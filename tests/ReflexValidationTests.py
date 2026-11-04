"""Exercise acceptance failures, incomplete runs and stable paired traces without a GPU."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import sys

sys.dont_write_bytecode = True

spec = importlib.util.spec_from_file_location('reflex_summary', Path(__file__).resolve().parents[1] / 'tools/Summarize-ReflexValidation.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ValidationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.manifest = {'start': 0, 'duration': 4, 'repeats': 2,
                         'allowedRegressionFraction': .05, 'runs': []}
        for repeat in (1, 2):
            for fps in ('unlimited', '240', 'vsync'):
                for mode in ('off', 'reflex'):
                    name = f'r{repeat}-gpu-{fps}-{mode}'
                    self.manifest['runs'].append({'name': name, 'mode': mode, 'exitCode': 0})
                    (self.root / f'{name}.csv').write_text(
                        'playhead_s,frame_ms,interval_ms,present_deferred\n1,1,4,0\n2,1,4,0\n3,1,4,0\n')
                    active = int(mode == 'reflex')
                    (self.root / f'{name}.log').write_text(
                        f'reflex_active={active} driver_low_latency={active} GPU fallback=0\n'
                        'audio_render_failures=0 audio_empty_padding_events=0\n')
        self.save()

    def save(self):
        (self.root / 'manifest.json').write_text(json.dumps(self.manifest))

    def test_stable_does_not_approve_release(self):
        result = module.summarize(self.root)
        self.assertTrue(result['stability_and_timing_passed'])
        self.assertFalse(result['release_approved'])

    def test_missing_counter_is_not_zero(self):
        (self.root / 'r1-gpu-240-reflex.log').write_text('reflex_active=1\n')
        self.assertFalse(module.summarize(self.root)['stability_and_timing_passed'])

    def test_regression_rejected(self):
        (self.root / 'r1-gpu-240-reflex.csv').write_text(
            'playhead_s,frame_ms,interval_ms,present_deferred\n1,10,40,0\n2,10,40,0\n3,10,40,0\n')
        self.assertFalse(module.summarize(self.root)['stability_and_timing_passed'])

    def test_incomplete_matrix_rejected(self):
        self.manifest['runs'].pop()
        self.save()
        with self.assertRaisesRegex(ValueError, 'Incomplete matrix'):
            module.summarize(self.root)

    def test_nonfinite_timing_rejected(self):
        with self.assertRaises(ValueError):
            module.distribution([float('nan')])

    def test_truncated_trace_rejected(self):
        (self.root / 'r1-gpu-240-reflex.csv').write_text(
            'playhead_s,frame_ms,interval_ms,present_deferred\n1,1,4,0\n1.1,1,4,0\n')
        with self.assertRaisesRegex(ValueError, 'Incomplete measurement window'):
            module.summarize(self.root)


if __name__ == '__main__':
    unittest.main()
