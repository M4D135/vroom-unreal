"""Explicit Mac release gates: 60 Hz normally, 30 Hz only for photorealism."""
import math


GRAPHICS_PRESETS = ('performance', 'balanced', 'high', 'ultra', 'photorealism')
PERFORMANCE_LIMITS = dict(min_mean_fps=59.0, max_p99_ms=18.0,
                          max_frame_ms=33.5, max_over_20_ms_fraction=.005)
PHOTOREALISM_PERFORMANCE_LIMITS = dict(min_mean_fps=29.7, max_p99_ms=35.0,
                                      max_frame_ms=50.0, max_over_40_ms_fraction=.005)
MOTION_LIMITS = dict(max_yaw_delta_degrees=3.0, max_yaw_acceleration=300.0)


def performance_limits_for_preset(preset):
    if preset is not None and preset not in GRAPHICS_PRESETS:
        raise ValueError(f'Unknown graphics preset: {preset}')
    return dict(PHOTOREALISM_PERFORMANCE_LIMITS if preset == 'photorealism' else PERFORMANCE_LIMITS)


def release_limits_for_preset(preset):
    return dict(performance=performance_limits_for_preset(preset), motion=dict(MOTION_LIMITS))


def check_release_limits(report, *, allow_legacy=False):
    """Reject edited/mislabeled budgets even if two reports contain the same edit.

    Old installers accepted pre-0.34 reports without embedded limits. Those
    rollbacks retain the original 60-Hz gates; absence never selects 30 Hz.
    """
    preset = report.get('graphics_preset')
    expected = release_limits_for_preset(preset)
    actual = report.get('release_limits')
    if actual is None and allow_legacy and preset != 'photorealism':
        return expected
    if actual != expected:
        raise ValueError('Release limits are missing or inconsistent with the graphics preset')
    return expected


def performance_failures(report, expected_frames, limits=None):
    limits = limits or PERFORMANCE_LIMITS
    failures = []
    threshold = 40 if 'max_over_40_ms_fraction' in limits else 20
    counter = f'over_{threshold}_ms_count'
    fraction_limit = f'max_over_{threshold}_ms_fraction'
    frames = report.get('frame_time', {})
    try:
        valid_metrics = all(isinstance(value, (float, int)) and not isinstance(value, bool)
                            and math.isfinite(value) and value > 0
                            for value in (report['mean_fps'], frames['p99_ms'], frames['max_ms']))
        valid_metrics &= (type(frames['samples']) is int and frames['samples'] > 0
                          and type(frames[counter]) is int and 0 <= frames[counter] <= frames['samples'])
    except (KeyError, TypeError):
        valid_metrics = False
    if not valid_metrics:
        return ['Missing or invalid performance metrics']
    if report['numeric_frame_rows'] != expected_frames or report['invalid_frames_after_warmup']:
        failures.append('Incomplete or invalid frame capture')
    if report['mean_fps'] < limits['min_mean_fps']:
        failures.append(f"Mean FPS {report['mean_fps']:.3f} < {limits['min_mean_fps']}")
    for key, limit in [('p99_ms', 'max_p99_ms'), ('max_ms', 'max_frame_ms')]:
        if frames[key] > limits[limit]:
            failures.append(f"{key} {frames[key]:.3f} > {limits[limit]}")
    if frames[counter] / frames['samples'] > limits[fraction_limit]:
        failures.append(f'Too many frames above {threshold} ms')
    return failures


def motion_failures(report, min_samples=1000, limits=None):
    limits = limits or MOTION_LIMITS
    failures = []
    if report['analyzed_samples'] < min_samples or not report['all_analyzed_telemetry_valid']:
        failures.append('Incomplete or invalid motion capture')
    if report['distance_backsteps_count']:
        failures.append('Presentation moved backwards along the track')
    for field, limit in [('yaw_delta_degrees', 'max_yaw_delta_degrees'),
                         ('yaw_acceleration_degrees_per_second_squared', 'max_yaw_acceleration')]:
        value = report[field]
        if value is None or value['max_absolute'] > limits[limit]:
            failures.append(f'Camera discontinuity: {field} exceeds {limits[limit]}')
    return failures
