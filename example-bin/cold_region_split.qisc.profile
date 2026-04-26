{
  "version": 1,
  "timestamp": 1777205355,
  "run_count": 12,
  "ir_hash": 909043091152894524,
  "has_converged": true,
  "functions": [
    {"name": "main", "call_count": 12, "total_cycles": 5702, "is_hot": true, "is_cold": false, "should_inline": false}
  ],
  "branches": [
    {"location": "main:4", "taken_count": 119964, "not_taken_count": 48, "taken_ratio": 0.9996, "is_predictable": true},
    {"location": "main:7", "taken_count": 24, "not_taken_count": 24, "taken_ratio": 0.5000, "is_predictable": false}
  ],
  "loops": [
    {"location": "main:3", "invocation_count": 12, "total_iterations": 120012, "avg_iterations": 10001.00, "should_unroll": false, "suggested_unroll_factor": 1}
  ]
}
