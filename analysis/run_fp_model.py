"""Run fp_model.py while removing Stan's repetitive timing estimates.

All command-line arguments and stdin are passed through to fp_model.py. Output
is streamed immediately so Stan iteration updates and suspension prompts remain
visible, while the unhelpful gradient-timing block printed for every chain is
discarded.
"""

import os
from pathlib import Path
import subprocess
import sys
import time


FILTERED_PREFIXES = (
    "Gradient evaluation took ",
    "1000 transitions using 10 leapfrog steps per transition would take ",
    "Adjust your expectations accordingly!",
)
ITERATION_PREFIX = "Iteration:"
ELAPSED_TIME_PREFIXES = (
    "Elapsed Time:",
    " Elapsed Time:",
)
ITERATION_INTERVAL_SECONDS = 5.0
SPECIAL_PREFIXES = (
    *FILTERED_PREFIXES,
    *ELAPSED_TIME_PREFIXES,
    ITERATION_PREFIX,
)


class StanOutputFilter:
    """Filter selected complete lines without buffering unrelated output."""

    def __init__(
        self,
        output,
        iteration_interval=ITERATION_INTERVAL_SECONDS,
        clock=time.monotonic,
    ):
        self.output = output
        self.iteration_interval = iteration_interval
        self.clock = clock
        self.prefix_buffer = ""
        self.passthrough = False
        self.suppress = False
        self.capture_iteration = False
        self.capture_elapsed_time = False
        self.elapsed_time_lines = []
        self.elapsed_interleaved_lines = []
        self.pending_blank = False
        self.just_suppressed = False
        self.last_iteration_output = None

    def write(self, text):
        for character in text:
            self._write_character(character)

    def finish(self):
        if self.passthrough:
            return
        if self.capture_elapsed_time:
            if self.prefix_buffer:
                self.elapsed_time_lines.append(self.prefix_buffer)
                self.prefix_buffer = ""
            self._emit_elapsed_time()
            return
        if self.capture_iteration:
            self._finish_iteration("")
            return
        if self.prefix_buffer:
            self._begin_passthrough()
        elif self.pending_blank and not self.just_suppressed:
            self.output.write("\n")
            self.output.flush()

    def _write_character(self, character):
        if self.suppress:
            if character == "\n":
                self.suppress = False
                self.just_suppressed = True
                self.pending_blank = False
            return

        if self.capture_elapsed_time:
            if character == "\n":
                self._finish_elapsed_time_line()
            else:
                self.prefix_buffer += character
            return

        if self.capture_iteration:
            if character == "\n":
                self._finish_iteration(character)
            else:
                self.prefix_buffer += character
            return

        if self.passthrough:
            self.output.write(character)
            self.output.flush()
            if character == "\n":
                self.passthrough = False
                self.just_suppressed = False
            return

        if character == "\n":
            if self.prefix_buffer:
                self._begin_passthrough()
                self.output.write(character)
                self.output.flush()
                self.passthrough = False
                self.just_suppressed = False
            else:
                self.pending_blank = True
            return

        self.prefix_buffer += character
        if self.prefix_buffer in FILTERED_PREFIXES:
            self.prefix_buffer = ""
            self.suppress = True
            self.pending_blank = False
            return

        if self.prefix_buffer in ELAPSED_TIME_PREFIXES:
            self.capture_elapsed_time = True
            self.pending_blank = False
            return

        if self.prefix_buffer == ITERATION_PREFIX:
            self.capture_iteration = True
            return

        if not any(
            prefix.startswith(self.prefix_buffer)
            for prefix in SPECIAL_PREFIXES
        ):
            self._begin_passthrough()

    def _finish_iteration(self, ending):
        now = self.clock()
        should_output = (
            self.last_iteration_output is None
            or now - self.last_iteration_output
            >= self.iteration_interval
        )
        if should_output:
            self.output.write(self.prefix_buffer)
            self.output.write(ending)
            self.output.flush()
            self.last_iteration_output = now

        self.prefix_buffer = ""
        self.capture_iteration = False
        self.pending_blank = False
        self.just_suppressed = False

    def _finish_elapsed_time_line(self):
        line = self.prefix_buffer
        self.prefix_buffer = ""
        stripped_line = line.strip()
        if not stripped_line:
            return

        expected_line = (
            not self.elapsed_time_lines
            or stripped_line.endswith("(Sampling)")
            or stripped_line.endswith("(Total)")
        )
        if expected_line:
            self.elapsed_time_lines.append(stripped_line)
        else:
            # Parallel chains can theoretically interleave output. Preserve
            # anything that is not part of this timing block for replay.
            self.elapsed_interleaved_lines.append(line + "\n")

        if stripped_line.endswith("(Total)"):
            self._emit_elapsed_time()

    def _emit_elapsed_time(self):
        if self.elapsed_time_lines:
            self.output.write(", ".join(
                line.strip() for line in self.elapsed_time_lines
            ))
            self.output.write("\n")
            self.output.flush()

        interleaved_lines = self.elapsed_interleaved_lines
        self.elapsed_time_lines = []
        self.elapsed_interleaved_lines = []
        self.capture_elapsed_time = False
        self.pending_blank = False
        self.just_suppressed = True

        for line in interleaved_lines:
            self.write(line)

    def _begin_passthrough(self):
        if self.pending_blank and not self.just_suppressed:
            self.output.write("\n")
        self.pending_blank = False
        self.just_suppressed = False
        self.output.write(self.prefix_buffer)
        self.output.flush()
        self.prefix_buffer = ""
        self.passthrough = True


def run(arguments):
    script = Path(__file__).resolve().with_name("fp_model.py")
    environment = os.environ.copy()
    environment["PYTHONUNBUFFERED"] = "1"

    process = subprocess.Popen(
        [sys.executable, str(script), *arguments],
        stdin=None,
        stdout=subprocess.PIPE,
        stderr=None,
        text=True,
        bufsize=0,
        env=environment,
    )
    output_filter = StanOutputFilter(sys.stdout)

    try:
        while True:
            character = process.stdout.read(1)
            if not character:
                break
            output_filter.write(character)
    finally:
        output_filter.finish()
        process.stdout.close()

    return process.wait()


if __name__ == "__main__":
    raise SystemExit(run(sys.argv[1:]))
