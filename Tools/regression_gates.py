"""Explicit release gates for the 60-Hz Mac profile; never silently relax them."""
PERFORMANCE_LIMITS = dict(min_mean_fps=59.0, max_p99_ms=18.0,
                          max_frame_ms=33.5, max_over_20_ms_fraction=.005)
MOTION_LIMITS = dict(max_yaw_delta_degrees=3.0, max_yaw_acceleration=300.0)


def performance_failures(report, expected_frames, limits=None):
    limits = limits or PERFORMANCE_LIMITS
    failures = []
    if report['numeric_frame_rows'] != expected_frames or report['invalid_frames_after_warmup']:
        failures.append('Incomplete or invalid frame capture')
    if report['mean_fps'] < limits['min_mean_fps']:
        failures.append(f"Mean FPS {report['mean_fps']:.3f} < {limits['min_mean_fps']}")
    frames = report['frame_time']
    for key, limit in [('p99_ms', 'max_p99_ms'), ('max_ms', 'max_frame_ms')]:
        if frames[key] > limits[limit]:
            failures.append(f"{key} {frames[key]:.3f} > {limits[limit]}")
    if frames['over_20_ms_count'] / frames['samples'] > limits['max_over_20_ms_fraction']:
        failures.append('Too many frames above 20 ms')
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
