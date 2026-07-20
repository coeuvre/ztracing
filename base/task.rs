//! Bounded task submission with cooperative cancellation and owned completions.

use std::collections::{HashMap, VecDeque};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Condvar, Mutex};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

pub type SubmissionId = u64;

pub type ExecutorJob = Box<dyn FnOnce() + Send + 'static>;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ExecuteError {
    ShuttingDown,
}

pub trait Executor: Send + Sync + 'static {
    fn execute(&self, job: ExecutorJob) -> Result<(), ExecuteError>;
}

struct ExecutorState {
    jobs: VecDeque<ExecutorJob>,
    shutting_down: bool,
}

struct ExecutorShared {
    state: Mutex<ExecutorState>,
    work_available: Condvar,
}

/// A fixed-size thread-pool executor implemented with Rust standard-library primitives.
pub struct ThreadPoolExecutor {
    shared: Arc<ExecutorShared>,
    workers: Vec<JoinHandle<()>>,
}

impl ThreadPoolExecutor {
    pub fn new(worker_count: usize) -> Self {
        assert!(worker_count > 0, "thread pool needs at least one worker");
        let shared = Arc::new(ExecutorShared {
            state: Mutex::new(ExecutorState {
                jobs: VecDeque::new(),
                shutting_down: false,
            }),
            work_available: Condvar::new(),
        });
        let workers = (0..worker_count)
            .map(|_| {
                let shared = Arc::clone(&shared);
                thread::spawn(move || executor_worker_loop(shared))
            })
            .collect();
        Self { shared, workers }
    }
}

impl Executor for ThreadPoolExecutor {
    fn execute(&self, job: ExecutorJob) -> Result<(), ExecuteError> {
        let mut state = self.shared.state.lock().expect("executor mutex poisoned");
        if state.shutting_down {
            return Err(ExecuteError::ShuttingDown);
        }
        state.jobs.push_back(job);
        self.shared.work_available.notify_one();
        Ok(())
    }
}

fn executor_worker_loop(shared: Arc<ExecutorShared>) {
    loop {
        let job = {
            let mut state = shared.state.lock().expect("executor mutex poisoned");
            while state.jobs.is_empty() && !state.shutting_down {
                state = shared
                    .work_available
                    .wait(state)
                    .expect("executor mutex poisoned");
            }
            if state.shutting_down && state.jobs.is_empty() {
                return;
            }
            state.jobs.pop_front().expect("executor job exists")
        };
        job();
    }
}

impl Drop for ThreadPoolExecutor {
    fn drop(&mut self) {
        {
            let mut state = self.shared.state.lock().expect("executor mutex poisoned");
            state.shutting_down = true;
        }
        self.shared.work_available.notify_all();
        for worker in self.workers.drain(..) {
            let _ = worker.join();
        }
    }
}

#[derive(Clone)]
pub struct CancelToken(Arc<AtomicBool>);

impl CancelToken {
    fn new() -> Self {
        Self(Arc::new(AtomicBool::new(false)))
    }

    pub fn is_cancelled(&self) -> bool {
        self.0.load(Ordering::Acquire)
    }

    fn cancel(&self) {
        self.0.store(true, Ordering::Release);
    }
}

pub struct TaskHandle {
    id: SubmissionId,
    token: CancelToken,
}

impl TaskHandle {
    pub fn id(&self) -> SubmissionId {
        self.id
    }

    pub fn cancel(&self) {
        self.token.cancel();
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Status {
    Ok,
    Failed,
    Cancelled,
    Panicked,
}

pub struct Completion<T> {
    pub id: SubmissionId,
    pub status: Status,
    pub output: Option<T>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SubmitError {
    Full,
    Timeout,
    ShuttingDown,
}

#[derive(Clone, Copy)]
enum WaitMode {
    None,
    Forever,
    Until(Instant),
}

struct State<T> {
    active: HashMap<SubmissionId, CancelToken>,
    completions: VecDeque<Completion<T>>,
    shutting_down: bool,
}

struct Shared<T> {
    capacity: usize,
    state: Mutex<State<T>>,
    space_available: Condvar,
    completion_available: Condvar,
    next_id: AtomicU64,
}

pub struct TaskQueue<T: Send + 'static> {
    executor: Arc<dyn Executor>,
    shared: Arc<Shared<T>>,
}

impl<T: Send + 'static> TaskQueue<T> {
    pub fn new(executor: Arc<dyn Executor>, capacity: usize) -> Self {
        assert!(capacity > 0, "task queue capacity must be positive");
        Self {
            executor,
            shared: Arc::new(Shared {
                capacity,
                state: Mutex::new(State {
                    active: HashMap::new(),
                    completions: VecDeque::with_capacity(capacity),
                    shutting_down: false,
                }),
                space_available: Condvar::new(),
                completion_available: Condvar::new(),
                next_id: AtomicU64::new(1),
            }),
        }
    }

    pub fn try_submit<F>(&self, job: F) -> Result<TaskHandle, SubmitError>
    where
        F: FnOnce(&CancelToken) -> Result<T, ()> + Send + 'static,
    {
        self.submit_impl(job, WaitMode::None)
    }

    pub fn submit<F>(&self, job: F) -> Result<TaskHandle, SubmitError>
    where
        F: FnOnce(&CancelToken) -> Result<T, ()> + Send + 'static,
    {
        self.submit_impl(job, WaitMode::Forever)
    }

    pub fn submit_timeout<F>(&self, timeout: Duration, job: F) -> Result<TaskHandle, SubmitError>
    where
        F: FnOnce(&CancelToken) -> Result<T, ()> + Send + 'static,
    {
        self.submit_impl(job, WaitMode::Until(Instant::now() + timeout))
    }

    fn submit_impl<F>(&self, job: F, wait: WaitMode) -> Result<TaskHandle, SubmitError>
    where
        F: FnOnce(&CancelToken) -> Result<T, ()> + Send + 'static,
    {
        let id = self.shared.next_id.fetch_add(1, Ordering::Relaxed);
        let token = CancelToken::new();
        {
            let mut state = self.shared.state.lock().expect("task queue mutex poisoned");
            while state.active.len() + state.completions.len() >= self.shared.capacity
                && !state.shutting_down
            {
                state = match wait {
                    WaitMode::None => return Err(SubmitError::Full),
                    WaitMode::Forever => self
                        .shared
                        .space_available
                        .wait(state)
                        .expect("task queue mutex poisoned"),
                    WaitMode::Until(deadline) => {
                        let now = Instant::now();
                        if now >= deadline {
                            return Err(SubmitError::Timeout);
                        }
                        let (state, result) = self
                            .shared
                            .space_available
                            .wait_timeout(state, deadline - now)
                            .expect("task queue mutex poisoned");
                        if result.timed_out()
                            && state.active.len() + state.completions.len() >= self.shared.capacity
                        {
                            return Err(SubmitError::Timeout);
                        }
                        state
                    }
                };
            }
            if state.shutting_down {
                return Err(SubmitError::ShuttingDown);
            }
            state.active.insert(id, token.clone());
        }

        let shared = Arc::clone(&self.shared);
        let worker_token = token.clone();
        let execute = self.executor.execute(Box::new(move || {
            let completion = run_job(id, &worker_token, job);
            let mut state = shared.state.lock().expect("task queue mutex poisoned");
            state.active.remove(&id);
            if !state.shutting_down {
                state.completions.push_back(completion);
                shared.completion_available.notify_one();
            }
        }));
        if execute.is_err() {
            let mut state = self.shared.state.lock().expect("task queue mutex poisoned");
            state.active.remove(&id);
            self.shared.space_available.notify_all();
            return Err(SubmitError::ShuttingDown);
        }
        Ok(TaskHandle { id, token })
    }

    pub fn try_completion(&self) -> Option<Completion<T>> {
        let mut state = self.shared.state.lock().expect("task queue mutex poisoned");
        let completion = state.completions.pop_front();
        if completion.is_some() {
            self.shared.space_available.notify_all();
        }
        completion
    }

    pub fn wait_completion_timeout(&self, timeout: Duration) -> Option<Completion<T>> {
        let deadline = Instant::now() + timeout;
        let mut state = self.shared.state.lock().expect("task queue mutex poisoned");
        loop {
            if let Some(completion) = state.completions.pop_front() {
                self.shared.space_available.notify_all();
                return Some(completion);
            }
            let now = Instant::now();
            if now >= deadline {
                return None;
            }
            let (next, result) = self
                .shared
                .completion_available
                .wait_timeout(state, deadline - now)
                .expect("task queue mutex poisoned");
            state = next;
            if result.timed_out() && state.completions.is_empty() {
                return None;
            }
        }
    }
}

fn run_job<T, F>(id: SubmissionId, token: &CancelToken, job: F) -> Completion<T>
where
    F: FnOnce(&CancelToken) -> Result<T, ()>,
{
    if token.is_cancelled() {
        return Completion {
            id,
            status: Status::Cancelled,
            output: None,
        };
    }
    match catch_unwind(AssertUnwindSafe(|| job(token))) {
        Ok(Ok(_)) if token.is_cancelled() => Completion {
            id,
            status: Status::Cancelled,
            output: None,
        },
        Ok(Ok(output)) => Completion {
            id,
            status: Status::Ok,
            output: Some(output),
        },
        Ok(Err(())) if token.is_cancelled() => Completion {
            id,
            status: Status::Cancelled,
            output: None,
        },
        Ok(Err(())) => Completion {
            id,
            status: Status::Failed,
            output: None,
        },
        Err(_) => Completion {
            id,
            status: Status::Panicked,
            output: None,
        },
    }
}

impl<T: Send + 'static> Drop for TaskQueue<T> {
    fn drop(&mut self) {
        let mut state = self.shared.state.lock().expect("task queue mutex poisoned");
        state.shutting_down = true;
        for token in state.active.values() {
            token.cancel();
        }
        self.shared.space_available.notify_all();
        self.shared.completion_available.notify_all();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicBool, AtomicUsize};
    use std::sync::{Barrier, mpsc};

    struct InlineExecutor;

    impl Executor for InlineExecutor {
        fn execute(&self, job: ExecutorJob) -> Result<(), ExecuteError> {
            job();
            Ok(())
        }
    }

    struct RejectingExecutor;

    impl Executor for RejectingExecutor {
        fn execute(&self, _job: ExecutorJob) -> Result<(), ExecuteError> {
            Err(ExecuteError::ShuttingDown)
        }
    }

    fn queue<T: Send + 'static>(capacity: usize, workers: usize) -> TaskQueue<T> {
        TaskQueue::new(Arc::new(ThreadPoolExecutor::new(workers)), capacity)
    }

    #[test]
    fn executes_and_returns_owned_output() {
        let queue = queue(2, 1);
        queue.try_submit(|_| Ok(vec![1, 2, 3])).unwrap();
        let completion = queue
            .wait_completion_timeout(Duration::from_secs(1))
            .unwrap();
        assert_eq!(completion.status, Status::Ok);
        assert_eq!(completion.output, Some(vec![1, 2, 3]));
    }

    #[test]
    fn cancellation_is_cooperative() {
        let queue = queue(1, 1);
        let handle = queue
            .try_submit(|cancel| {
                while !cancel.is_cancelled() {
                    thread::yield_now();
                }
                Ok(())
            })
            .unwrap();
        handle.cancel();
        assert_eq!(
            queue
                .wait_completion_timeout(Duration::from_secs(1))
                .unwrap()
                .status,
            Status::Cancelled
        );
    }

    #[test]
    fn cancelled_queued_job_never_runs() {
        let queue = queue(2, 1);
        let barrier = Arc::new(Barrier::new(2));
        let worker_barrier = Arc::clone(&barrier);
        queue
            .try_submit(move |_| {
                worker_barrier.wait();
                Ok(false)
            })
            .unwrap();
        let handle = queue.try_submit(|_| Ok(true)).unwrap();
        handle.cancel();
        barrier.wait();
        let completions = [
            queue
                .wait_completion_timeout(Duration::from_secs(1))
                .unwrap(),
            queue
                .wait_completion_timeout(Duration::from_secs(1))
                .unwrap(),
        ];
        assert!(completions.iter().any(
            |completion| completion.id == handle.id() && completion.status == Status::Cancelled
        ));
    }

    #[test]
    fn submission_ids_are_unique() {
        let queue = queue(2, 1);
        let first = queue.try_submit(|_| Ok(())).unwrap();
        let second = queue.try_submit(|_| Ok(())).unwrap();
        assert_ne!(first.id(), second.id());
    }

    #[test]
    fn try_submit_reports_full_queue() {
        let queue = queue(1, 1);
        let barrier = Arc::new(Barrier::new(2));
        let worker_barrier = Arc::clone(&barrier);
        queue
            .try_submit(move |_| {
                worker_barrier.wait();
                Ok(())
            })
            .unwrap();
        assert!(matches!(
            queue.try_submit(|_| Ok(())),
            Err(SubmitError::Full)
        ));
        barrier.wait();
    }

    #[test]
    fn submit_timeout_bounds_wait_for_completion_space() {
        let queue = TaskQueue::new(Arc::new(InlineExecutor), 1);
        queue.submit(|_| Ok(1)).unwrap();
        assert!(matches!(
            queue.submit_timeout(Duration::from_millis(1), |_| Ok(2)),
            Err(SubmitError::Timeout)
        ));
        assert_eq!(queue.try_completion().unwrap().output, Some(1));
    }

    #[test]
    fn blocking_submit_from_producer_resumes_after_reaping() {
        let queue = Arc::new(TaskQueue::new(Arc::new(InlineExecutor), 1));
        queue.submit(|_| Ok(1)).unwrap();
        let producer_queue = Arc::clone(&queue);
        let (submitted, submitted_rx) = mpsc::channel();
        let producer = thread::spawn(move || {
            producer_queue.submit(|_| Ok(2)).unwrap();
            submitted.send(()).unwrap();
        });
        assert!(
            submitted_rx
                .recv_timeout(Duration::from_millis(10))
                .is_err()
        );
        assert_eq!(queue.try_completion().unwrap().output, Some(1));
        submitted_rx.recv_timeout(Duration::from_secs(1)).unwrap();
        producer.join().unwrap();
        assert_eq!(queue.try_completion().unwrap().output, Some(2));
    }

    #[test]
    fn inline_executor_is_supported() {
        let queue = TaskQueue::new(Arc::new(InlineExecutor), 1);
        queue.try_submit(|_| Ok(7)).unwrap();
        assert_eq!(queue.try_completion().unwrap().output, Some(7));
    }

    #[test]
    fn multiple_queues_share_one_executor() {
        let executor = Arc::new(ThreadPoolExecutor::new(1));
        let first = TaskQueue::new(executor.clone(), 1);
        let second = TaskQueue::new(executor, 1);
        first.try_submit(|_| Ok("first")).unwrap();
        second.try_submit(|_| Ok("second")).unwrap();
        assert_eq!(
            first
                .wait_completion_timeout(Duration::from_secs(1))
                .unwrap()
                .output,
            Some("first")
        );
        assert_eq!(
            second
                .wait_completion_timeout(Duration::from_secs(1))
                .unwrap()
                .output,
            Some("second")
        );
    }

    #[test]
    fn executor_rejection_is_reported_and_releases_capacity() {
        let queue = TaskQueue::<()>::new(Arc::new(RejectingExecutor), 1);
        assert!(matches!(
            queue.try_submit(|_| Ok(())),
            Err(SubmitError::ShuttingDown)
        ));
        assert!(matches!(
            queue.try_submit(|_| Ok(())),
            Err(SubmitError::ShuttingDown)
        ));
    }

    #[test]
    fn unconsumed_completion_applies_backpressure() {
        let queue = TaskQueue::new(Arc::new(InlineExecutor), 1);
        queue.try_submit(|_| Ok(1)).unwrap();
        assert!(matches!(
            queue.try_submit(|_| Ok(2)),
            Err(SubmitError::Full)
        ));
        assert_eq!(queue.try_completion().unwrap().output, Some(1));
        queue.try_submit(|_| Ok(2)).unwrap();
        assert_eq!(queue.try_completion().unwrap().output, Some(2));
    }

    #[test]
    fn cancellation_takes_precedence_over_failure() {
        let queue = queue::<()>(1, 1);
        let started = Arc::new(Barrier::new(2));
        let finish = Arc::new(Barrier::new(2));
        let worker_started = Arc::clone(&started);
        let worker_finish = Arc::clone(&finish);
        let handle = queue
            .try_submit(move |_| {
                worker_started.wait();
                worker_finish.wait();
                Err(())
            })
            .unwrap();
        started.wait();
        handle.cancel();
        finish.wait();
        assert_eq!(
            queue
                .wait_completion_timeout(Duration::from_secs(1))
                .unwrap()
                .status,
            Status::Cancelled
        );
    }

    #[test]
    fn queued_cancellation_does_not_run_task_body() {
        let executor = Arc::new(ThreadPoolExecutor::new(1));
        let queue = TaskQueue::new(executor, 2);
        let barrier = Arc::new(Barrier::new(2));
        let worker_barrier = Arc::clone(&barrier);
        queue
            .try_submit(move |_| {
                worker_barrier.wait();
                Ok(())
            })
            .unwrap();
        let ran = Arc::new(AtomicBool::new(false));
        let worker_ran = Arc::clone(&ran);
        let handle = queue
            .try_submit(move |_| {
                worker_ran.store(true, Ordering::Release);
                Ok(())
            })
            .unwrap();
        handle.cancel();
        barrier.wait();
        for _ in 0..2 {
            queue
                .wait_completion_timeout(Duration::from_secs(1))
                .unwrap();
        }
        assert!(!ran.load(Ordering::Acquire));
    }

    #[test]
    fn single_worker_completes_jobs_in_submission_order() {
        let queue = queue(3, 1);
        let sequence = Arc::new(AtomicUsize::new(0));
        for expected in 0..3 {
            let sequence = Arc::clone(&sequence);
            queue
                .try_submit(move |_| Ok((expected, sequence.fetch_add(1, Ordering::AcqRel))))
                .unwrap();
        }
        for expected in 0..3 {
            assert_eq!(
                queue
                    .wait_completion_timeout(Duration::from_secs(1))
                    .unwrap()
                    .output,
                Some((expected, expected))
            );
        }
    }

    #[test]
    fn panic_does_not_kill_executor_worker() {
        let queue = queue(2, 1);
        queue
            .try_submit(|_| -> Result<i32, ()> { panic!("boom") })
            .unwrap();
        queue.try_submit(|_| Ok(7)).unwrap();
        assert_eq!(
            queue
                .wait_completion_timeout(Duration::from_secs(1))
                .unwrap()
                .status,
            Status::Panicked
        );
        assert_eq!(
            queue
                .wait_completion_timeout(Duration::from_secs(1))
                .unwrap()
                .output,
            Some(7)
        );
    }

    #[test]
    fn timeout_expires_without_completion() {
        let queue = queue::<()>(1, 1);
        assert!(
            queue
                .wait_completion_timeout(Duration::from_millis(1))
                .is_none()
        );
    }

    #[test]
    fn dropping_queue_cancels_active_job() {
        let executor = Arc::new(ThreadPoolExecutor::new(1));
        let queue = TaskQueue::new(executor, 1);
        queue
            .try_submit(|cancel| {
                while !cancel.is_cancelled() {
                    thread::yield_now();
                }
                Ok(())
            })
            .unwrap();
        drop(queue);
    }

    #[test]
    #[should_panic]
    fn zero_capacity_is_rejected() {
        let _ = TaskQueue::<()>::new(Arc::new(ThreadPoolExecutor::new(1)), 0);
    }
}
