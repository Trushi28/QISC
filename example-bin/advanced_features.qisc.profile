{
  "version": 1,
  "timestamp": 1774635100,
  "run_count": 1,
  "ir_hash": 0,
  "has_converged": false,
  "functions": [
    {"name": "main", "call_count": 1, "total_cycles": 57, "is_hot": true, "is_cold": false, "should_inline": false},
    {"name": "risky", "call_count": 1, "total_cycles": 0, "is_hot": false, "is_cold": true, "should_inline": false},
    {"name": "add", "call_count": 1, "total_cycles": 2, "is_hot": false, "is_cold": false, "should_inline": false},
    {"name": "greet", "call_count": 1, "total_cycles": 3, "is_hot": false, "is_cold": false, "should_inline": false}
  ],
  "branches": [
  ],
  "loops": [
    {"location": "main:88", "invocation_count": 1, "total_iterations": 5, "avg_iterations": 5.00, "should_unroll": true, "suggested_unroll_factor": 4}
  ]
}
