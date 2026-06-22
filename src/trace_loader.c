#include "src/trace_loader.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

#include "src/trace_parser.h"

// Synchronously loads a Chrome trace file, preferring success path under if and
// SESE.
trace_data_t* trace_loader_load_file(const char* filename, allocator_t a,
                                     size_t* out_decompressed_size) {
  trace_data_t* td = nullptr;
  FILE* f = fopen(filename, "rb");

  if (f) {
    // Read first 2 bytes to check gzip magic
    unsigned char magic[2];
    size_t magic_read = fread(magic, 1, 2, f);

    // Seek back to the beginning of the file
    fseek(f, 0, SEEK_SET);

    bool is_gzip = (magic_read == 2 && magic[0] == 0x1f && magic[1] == 0x8b);
    bool init_success = true;
    size_t decompressed_size_accum = 0;

    td = trace_data_create(a);
    trace_event_matcher_t matcher = {};
    trace_parser_t parser = {};

    char in_buf[65536];
    char out_buf[65536];
    bool is_eof = false;

    if (is_gzip) {
      z_stream strm = {};  // ZII
      if (inflateInit2(&strm, 16 + MAX_WBITS) == Z_OK) {
        int status = Z_OK;
        while (status != Z_STREAM_END && !is_eof) {
          if (strm.avail_in == 0) {
            size_t n = fread(in_buf, 1, sizeof(in_buf), f);
            if (n < sizeof(in_buf)) {
              is_eof = true;
            }
            strm.next_in = (Bytef*)in_buf;
            strm.avail_in = (uInt)n;
          }

          do {
            strm.next_out = (Bytef*)out_buf;
            strm.avail_out = sizeof(out_buf);

            status = inflate(&strm, Z_NO_FLUSH);
            if (status == Z_NEED_DICT || status == Z_DATA_ERROR ||
                status == Z_MEM_ERROR) {
              fprintf(stderr, "Error: Gzip decompression failed (code %d)\n",
                      status);
              init_success = false;
              break;
            }

            size_t decompressed_size = sizeof(out_buf) - strm.avail_out;
            if (decompressed_size > 0) {
              decompressed_size_accum += decompressed_size;
              bool parser_eof = (status == Z_STREAM_END);
              size_t discarded = trace_parser_feed(
                  &parser, out_buf, decompressed_size, parser_eof, a);
              (void)discarded;

              trace_event_t ev;
              while (trace_parser_next(&parser, &ev, a)) {
                trace_data_add_event(td, &ev, &matcher, a);
              }
            }
          } while (strm.avail_out == 0 && status != Z_STREAM_END);

          if (!init_success) {
            break;
          }
        }
        inflateEnd(&strm);
      } else {
        fprintf(stderr, "Error: Failed to initialize zlib decompression\n");
        init_success = false;
      }
    } else {
      // Direct raw JSON reading loop
      while (!is_eof) {
        size_t n = fread(out_buf, 1, sizeof(out_buf), f);
        if (n < sizeof(out_buf)) {
          is_eof = true;
        }
        decompressed_size_accum += n;

        size_t discarded = trace_parser_feed(&parser, out_buf, n, is_eof, a);
        (void)discarded;

        trace_event_t ev;
        while (trace_parser_next(&parser, &ev, a)) {
          trace_data_add_event(td, &ev, &matcher, a);
        }
      }
    }

    fclose(f);
    trace_event_matcher_deinit(&matcher);
    trace_parser_deinit(&parser, a);

    if (!init_success) {
      trace_data_release(td, a);
      td = nullptr;
    } else {
      if (out_decompressed_size) {
        *out_decompressed_size = decompressed_size_accum;
      }
    }
  } else {
    fprintf(stderr, "Error: Failed to open trace file '%s'\n", filename);
  }

  return td;
}
