import io
import unittest

from run_fp_model import StanOutputFilter


class StanOutputFilterTests(unittest.TestCase):
    def filter(self, text):
        output = io.StringIO()
        output_filter = StanOutputFilter(output)
        output_filter.write(text)
        output_filter.finish()
        return output.getvalue()

    def test_removes_gradient_timing_block(self):
        source = (
            "Before\n\n"
            "Gradient evaluation took 7.9e-05 seconds\n"
            "1000 transitions using 10 leapfrog steps per transition would "
            "take 0.79 seconds.\n"
            "Adjust your expectations accordingly!\n\n\n"
            "Iteration:   1 / 300 [  0%]  (Warmup)\n"
        )

        self.assertEqual(
            self.filter(source),
            "Before\nIteration:   1 / 300 [  0%]  (Warmup)\n",
        )

    def test_preserves_other_stan_messages(self):
        message = "Warning: divergent transition after warmup\n"
        self.assertEqual(self.filter(message), message)

    def test_unterminated_prompt_is_streamed(self):
        output = io.StringIO()
        output_filter = StanOutputFilter(output)

        output_filter.write("Generation suspended. Press Enter to resume: ")

        self.assertEqual(
            output.getvalue(),
            "Generation suspended. Press Enter to resume: ",
        )

    def test_iteration_lines_are_limited_by_time(self):
        current_time = [100.0]
        output = io.StringIO()
        output_filter = StanOutputFilter(
            output,
            iteration_interval=5.0,
            clock=lambda: current_time[0],
        )

        output_filter.write("Iteration:   1 / 300 [  0%]  (Warmup)\n")
        current_time[0] += 2.0
        output_filter.write("Iteration:  30 / 300 [ 10%]  (Warmup)\n")
        current_time[0] += 3.0
        output_filter.write("Iteration:  60 / 300 [ 20%]  (Warmup)\n")

        self.assertEqual(
            output.getvalue(),
            (
                "Iteration:   1 / 300 [  0%]  (Warmup)\n"
                "Iteration:  60 / 300 [ 20%]  (Warmup)\n"
            ),
        )

    def test_new_fit_does_not_reset_iteration_limit(self):
        current_time = [100.0]
        output = io.StringIO()
        output_filter = StanOutputFilter(
            output,
            iteration_interval=5.0,
            clock=lambda: current_time[0],
        )

        output_filter.write("Iteration: 300 / 300 [100%]  (Sampling)\n")
        current_time[0] += 1.0
        output_filter.write("Beginning sampling for ALP FP ...\n")
        output_filter.write("Iteration:   1 / 300 [  0%]  (Warmup)\n")

        self.assertEqual(
            output.getvalue(),
            (
                "Iteration: 300 / 300 [100%]  (Sampling)\n"
                "Beginning sampling for ALP FP ...\n"
            ),
        )

    def test_elapsed_time_block_is_compressed(self):
        source = (
            "Before\n\n"
            " Elapsed Time: 13.5135 seconds (Warm-up)\n"
            "               0.31631 seconds (Sampling)\n"
            "               13.8298 seconds (Total)\n\n"
            "After\n"
        )

        self.assertEqual(
            self.filter(source),
            (
                "Before\n"
                "Elapsed Time: 13.5135 seconds (Warm-up), "
                "0.31631 seconds (Sampling), "
                "13.8298 seconds (Total)\n"
                "After\n"
            ),
        )

    def test_unexpected_elapsed_output_is_preserved(self):
        source = (
            "Elapsed Time: 1 second (Warm-up)\n"
            "Unexpected timing detail\n"
            "2 seconds (Total)\n"
        )

        self.assertEqual(
            self.filter(source),
            (
                "Elapsed Time: 1 second (Warm-up), 2 seconds (Total)\n"
                "Unexpected timing detail\n"
            ),
        )


if __name__ == "__main__":
    unittest.main()
