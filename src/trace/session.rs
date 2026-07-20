use base::task::{CancelToken, SubmissionId, SubmitError, TaskHandle, TaskQueue};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex, mpsc};
use std::time::{Duration, Instant};

use super::data::{EventMatcher, TraceData};
use super::parser::TraceParser;
use super::track::{Track, organize_tracks};

pub struct LoadedTrace {
    pub data: TraceData,
    pub tracks: Vec<Track>,
    pub minimum_timestamp: i64,
    pub maximum_timestamp: i64,
    pub decompressed_size: usize,
    pub ingestion_ms: f64,
    pub throughput_mb_per_second: f64,
    pub starvation_ms: f64,
    pub starvation_percent: f64,
    pub organization_ms: f64,
    pub total_ms: f64,
}

struct Chunk {
    bytes: Vec<u8>,
    eof: bool,
    progress: Arc<Progress>,
    accounted_bytes: usize,
}

impl Chunk {
    fn new(bytes: Vec<u8>, eof: bool, progress: Arc<Progress>) -> Self {
        let accounted_bytes = bytes.len();
        progress
            .buffered_bytes
            .fetch_add(accounted_bytes, Ordering::AcqRel);
        Self {
            bytes,
            eof,
            progress,
            accounted_bytes,
        }
    }

    fn into_bytes(mut self) -> Vec<u8> {
        std::mem::take(&mut self.bytes)
    }
}

impl Drop for Chunk {
    fn drop(&mut self) {
        self.progress
            .buffered_bytes
            .fetch_sub(self.accounted_bytes, Ordering::AcqRel);
    }
}

pub struct Progress {
    event_count: AtomicUsize,
    parsed_bytes: AtomicUsize,
    buffered_bytes: AtomicUsize,
}

impl Progress {
    pub fn event_count(&self) -> usize {
        self.event_count.load(Ordering::Acquire)
    }

    pub fn parsed_bytes(&self) -> usize {
        self.parsed_bytes.load(Ordering::Acquire)
    }

    pub fn buffered_bytes(&self) -> usize {
        self.buffered_bytes.load(Ordering::Acquire)
    }
}

pub struct LoadSession {
    submission: Mutex<SubmissionState>,
    progress: Arc<Progress>,
    worker: Option<TaskHandle>,
}

struct SubmissionState {
    sender: Option<mpsc::Sender<Chunk>>,
    closed: bool,
}

impl LoadSession {
    pub fn new<T, F>(queue: &TaskQueue<T>, complete: F) -> Result<Self, SubmitError>
    where
        T: Send + 'static,
        F: FnOnce(Result<LoadedTrace, String>) -> T + Send + 'static,
    {
        let (sender, chunks) = mpsc::channel();
        let started = Instant::now();
        let progress = Arc::new(Progress {
            event_count: AtomicUsize::new(0),
            parsed_bytes: AtomicUsize::new(0),
            buffered_bytes: AtomicUsize::new(0),
        });
        let worker_progress = Arc::clone(&progress);
        let worker = queue.try_submit(move |cancel| {
            let result = ingest(chunks, &worker_progress, cancel, started);
            if cancel.is_cancelled() {
                return Err(());
            }
            Ok(complete(result))
        })?;
        Ok(Self {
            submission: Mutex::new(SubmissionState {
                sender: Some(sender),
                closed: false,
            }),
            progress,
            worker: Some(worker),
        })
    }

    pub fn push(&self, bytes: Vec<u8>, eof: bool) -> Result<(), Vec<u8>> {
        let mut submission = self.submission.lock().expect("load session mutex poisoned");
        if submission.closed {
            return Err(bytes);
        }
        let Some(sender) = &submission.sender else {
            return Err(bytes);
        };
        let chunk = Chunk::new(bytes, eof, Arc::clone(&self.progress));
        match sender.send(chunk) {
            Ok(()) => {
                if eof {
                    submission.closed = true;
                    submission.sender.take();
                }
                Ok(())
            }
            Err(error) => {
                submission.closed = true;
                submission.sender.take();
                Err(error.0.into_bytes())
            }
        }
    }

    pub fn progress(&self) -> &Progress {
        &self.progress
    }

    pub fn task_id(&self) -> Option<SubmissionId> {
        self.worker.as_ref().map(TaskHandle::id)
    }

    pub fn cancel(&self) {
        if let Some(worker) = &self.worker {
            worker.cancel();
        }
        let mut submission = self.submission.lock().expect("load session mutex poisoned");
        submission.closed = true;
        submission.sender.take();
    }
}

impl Drop for LoadSession {
    fn drop(&mut self) {
        self.cancel();
    }
}

fn ingest(
    chunks: mpsc::Receiver<Chunk>,
    progress: &Progress,
    cancel: &CancelToken,
    started: Instant,
) -> Result<LoadedTrace, String> {
    let mut active_parse = Duration::ZERO;
    let mut parser = TraceParser::new();
    let mut data = TraceData::new();
    let mut matcher = EventMatcher::default();
    let mut saw_eof = false;
    loop {
        if cancel.is_cancelled() {
            return Err("trace loading cancelled".to_owned());
        }
        let chunk = match chunks.recv_timeout(Duration::from_millis(10)) {
            Ok(chunk) => chunk,
            Err(mpsc::RecvTimeoutError::Timeout) => continue,
            Err(mpsc::RecvTimeoutError::Disconnected) => break,
        };
        let len = chunk.bytes.len();
        if cancel.is_cancelled() {
            return Err("trace loading cancelled".to_owned());
        }
        let parse_started = Instant::now();
        parser.feed(&chunk.bytes, chunk.eof);
        while let Some(event) = parser.next_event() {
            data.add_event(&event, &mut matcher);
        }
        progress
            .event_count
            .store(data.events.len(), Ordering::Release);
        progress.parsed_bytes.fetch_add(len, Ordering::AcqRel);
        active_parse += parse_started.elapsed();
        if chunk.eof {
            saw_eof = true;
            break;
        }
    }
    if cancel.is_cancelled() {
        return Err("trace loading cancelled".to_owned());
    }
    if !saw_eof || parser.is_invalid() || !parser.is_complete() {
        return Err("invalid trace JSON".to_owned());
    }
    let decompressed_size = progress.parsed_bytes();
    let compact_started = Instant::now();
    data.compact();
    active_parse += compact_started.elapsed();
    let ingestion_ms = started.elapsed().as_secs_f64() * 1_000.0;
    let throughput_mb_per_second = if ingestion_ms == 0.0 {
        0.0
    } else {
        decompressed_size as f64 / (1024.0 * 1024.0) / (ingestion_ms / 1_000.0)
    };
    let starvation_ms = (ingestion_ms - active_parse.as_secs_f64() * 1_000.0).max(0.0);
    let starvation_percent = if ingestion_ms == 0.0 {
        0.0
    } else {
        starvation_ms / ingestion_ms * 100.0
    };
    let organized = Instant::now();
    let (tracks, minimum_timestamp, maximum_timestamp) = organize_tracks(&data);
    let organization_ms = organized.elapsed().as_secs_f64() * 1_000.0;
    Ok(LoadedTrace {
        data,
        tracks,
        minimum_timestamp,
        maximum_timestamp,
        decompressed_size,
        ingestion_ms,
        throughput_mb_per_second,
        starvation_ms,
        starvation_percent,
        organization_ms,
        total_ms: started.elapsed().as_secs_f64() * 1_000.0,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use base::task::{ExecuteError, Executor, ExecutorJob, Status, ThreadPoolExecutor};

    #[derive(Default)]
    struct ManualExecutor {
        job: Mutex<Option<ExecutorJob>>,
    }

    impl ManualExecutor {
        fn run(&self) {
            self.job
                .lock()
                .expect("manual executor mutex poisoned")
                .take()
                .expect("executor job exists")();
        }
    }

    impl Executor for ManualExecutor {
        fn execute(&self, job: ExecutorJob) -> Result<(), ExecuteError> {
            *self.job.lock().expect("manual executor mutex poisoned") = Some(job);
            Ok(())
        }
    }

    fn queue(capacity: usize, workers: usize) -> TaskQueue<Result<LoadedTrace, String>> {
        TaskQueue::new(Arc::new(ThreadPoolExecutor::new(workers)), capacity)
    }

    fn wait_for_load(
        queue: &TaskQueue<Result<LoadedTrace, String>>,
    ) -> base::task::Completion<Result<LoadedTrace, String>> {
        queue
            .wait_completion_timeout(Duration::from_secs(1))
            .expect("load task completed")
    }

    #[test]
    fn success_path_reaps_correctly() {
        let queue = queue(4, 1);
        let session = LoadSession::new(&queue, |result| result).unwrap();
        let input = br#"[{"name":"event","ph":"X","ts":1,"dur":2}]"#.to_vec();
        session.push(input.clone(), true).unwrap();
        let completion = wait_for_load(&queue);
        assert_eq!(completion.status, Status::Ok);
        let result = completion.output.unwrap().unwrap();
        assert_eq!(result.data.events.len(), 1);
        assert_eq!(result.decompressed_size, input.len());
        assert_eq!(session.progress().event_count(), 1);
        assert_eq!(session.progress().parsed_bytes(), input.len());
        assert_eq!(session.progress().buffered_bytes(), 0);
        assert!(result.ingestion_ms.is_finite());
        assert!(result.throughput_mb_per_second.is_finite());
        assert!(result.starvation_ms.is_finite());
        assert!(result.starvation_percent.is_finite());
        assert!(result.total_ms >= result.ingestion_ms);
    }

    #[test]
    fn parser_accepts_input_split_at_every_byte_boundary() {
        let input = br#"[{"name":"event","ph":"X","ts":1,"dur":2}]"#;
        let queue = queue(1, 1);
        let session = LoadSession::new(&queue, |result| result).unwrap();
        for byte in input {
            session.push(vec![*byte], false).unwrap();
        }
        session.push(Vec::new(), true).unwrap();
        let result = wait_for_load(&queue).output.unwrap().unwrap();
        assert_eq!(result.data.events.len(), 1);
        assert_eq!(result.decompressed_size, input.len());
        assert_eq!(session.progress().buffered_bytes(), 0);
    }

    #[test]
    fn legacy_begin_end_pair_with_trailing_nul_is_loaded() {
        let input = b"[
            {\"name\":\"event\",\"cat\":\"test\",\"ph\":\"B\",\"ts\":1000,\"pid\":1,\"tid\":1},
            {\"name\":\"event\",\"cat\":\"test\",\"ph\":\"E\",\"ts\":2000,\"pid\":1,\"tid\":1}
        ]\0";
        let queue = queue(1, 1);
        let session = LoadSession::new(&queue, |result| result).unwrap();
        session.push(input.to_vec(), true).unwrap();
        let result = wait_for_load(&queue).output.unwrap().unwrap();
        assert_eq!(result.data.events.len(), 1);
        assert_eq!(result.decompressed_size, input.len());
        assert_eq!(session.progress().buffered_bytes(), 0);
    }

    #[test]
    fn executor_wait_is_reported_as_parser_starvation() {
        let executor = Arc::new(ManualExecutor::default());
        let queue = TaskQueue::new(executor.clone(), 1);
        let session = LoadSession::new(&queue, |result| result).unwrap();
        session.push(b"[]".to_vec(), true).unwrap();
        std::thread::sleep(Duration::from_millis(20));
        executor.run();
        let result = wait_for_load(&queue).output.unwrap().unwrap();
        assert!(result.ingestion_ms >= 15.0);
        assert!(result.starvation_ms >= 15.0);
        assert!(result.starvation_percent > 90.0);
    }

    #[test]
    fn full_task_queue_rejects_a_new_session_without_blocking() {
        let executor = Arc::new(ManualExecutor::default());
        let queue = TaskQueue::new(executor.clone(), 1);
        let session = LoadSession::new(&queue, |_| ()).unwrap();

        let started = Instant::now();
        assert!(matches!(
            LoadSession::new(&queue, |_| ()),
            Err(SubmitError::Full)
        ));
        assert!(started.elapsed() < Duration::from_secs(1));

        session.cancel();
        executor.run();
        assert_eq!(
            queue
                .wait_completion_timeout(Duration::from_secs(1))
                .expect("cancelled task completed")
                .status,
            Status::Cancelled
        );
    }

    #[test]
    fn cancellation_before_start_discards_chunks_and_suppresses_callback() {
        let executor = Arc::new(ManualExecutor::default());
        let queue = TaskQueue::new(executor.clone(), 1);
        let callback_count = Arc::new(AtomicUsize::new(0));
        let callback_count_for_task = Arc::clone(&callback_count);
        let session = LoadSession::new(&queue, move |_| {
            callback_count_for_task.fetch_add(1, Ordering::AcqRel);
        })
        .unwrap();
        session.push(vec![1, 2, 3], false).unwrap();
        session.push(vec![4, 5], false).unwrap();
        assert_eq!(session.progress().buffered_bytes(), 5);
        session.cancel();
        assert!(session.push(Vec::new(), true).is_err());
        executor.run();
        let completion = queue
            .wait_completion_timeout(Duration::from_secs(1))
            .expect("cancelled task completed");
        assert_eq!(completion.status, Status::Cancelled);
        assert!(completion.output.is_none());
        assert_eq!(callback_count.load(Ordering::Acquire), 0);
        assert_eq!(session.progress().buffered_bytes(), 0);
    }

    #[test]
    fn cancellation_interrupts_worker_waiting_for_input() {
        let queue = queue(1, 1);
        let session = LoadSession::new(&queue, |result| result).unwrap();
        session.cancel();
        let completion = wait_for_load(&queue);
        assert_eq!(completion.status, Status::Cancelled);
        assert!(completion.output.is_none());
        assert_eq!(session.progress().buffered_bytes(), 0);
    }

    #[test]
    fn dropping_queue_interrupts_worker_waiting_for_input() {
        let started = Instant::now();
        let queue = queue(1, 1);
        let session = LoadSession::new(&queue, |result| result).unwrap();
        drop(queue);
        assert!(started.elapsed() < Duration::from_secs(1));
        drop(session);
    }

    #[test]
    fn eof_closes_submission_side() {
        let queue = queue(1, 1);
        let session = LoadSession::new(&queue, |result| result).unwrap();
        session.push(b"[]".to_vec(), true).unwrap();
        assert_eq!(session.push(b"[]".to_vec(), false), Err(b"[]".to_vec()));
        assert_eq!(session.push(Vec::new(), true), Err(Vec::new()));
        assert!(wait_for_load(&queue).output.unwrap().is_ok());
        assert_eq!(session.progress().buffered_bytes(), 0);
    }

    #[test]
    fn submission_does_not_block_while_worker_is_delayed() {
        let executor = Arc::new(ManualExecutor::default());
        let queue = TaskQueue::new(executor.clone(), 1);
        let session = LoadSession::new(&queue, |result| result).unwrap();
        for _ in 0..1_024 {
            session.push(vec![0; 1_024], false).unwrap();
        }
        assert_eq!(session.progress().buffered_bytes(), 1024 * 1024);
        session.cancel();
        executor.run();
        assert_eq!(
            wait_for_load(&queue).status,
            Status::Cancelled,
            "delayed worker observes cancellation"
        );
        assert_eq!(session.progress().buffered_bytes(), 0);
    }

    #[test]
    fn malformed_or_incomplete_eof_reports_error() {
        for input in [br#"[{"ts":01}]"#.as_slice(), br#"[{"name":"partial"}"#] {
            let queue = queue(1, 1);
            let session = LoadSession::new(&queue, |result| result).unwrap();
            session.push(input.to_vec(), true).unwrap();
            let result = wait_for_load(&queue).output.unwrap();
            match result {
                Err(error) => assert_eq!(error, "invalid trace JSON"),
                Ok(_) => panic!("malformed or incomplete JSON was accepted"),
            }
        }
    }
}
