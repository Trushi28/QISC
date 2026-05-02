{
  "version": 1,
  "timestamp": 1777221126,
  "run_count": 2,
  "ir_hash": 14652728300103033269,
  "has_converged": true,
  "functions": [
    {"name": "main", "call_count": 2, "total_cycles": 42108, "is_hot": true, "is_cold": false, "should_inline": false},
    {"name": "hot_loop_a", "call_count": 2, "total_cycles": 21155, "is_hot": true, "is_cold": false, "should_inline": false},
    {"name": "shared", "call_count": 12002, "total_cycles": 20632, "is_hot": true, "is_cold": false, "should_inline": true},
    {"name": "hot_loop_b", "call_count": 2, "total_cycles": 20922, "is_hot": true, "is_cold": false, "should_inline": false},
    {"name": "cold_once", "call_count": 2, "total_cycles": 10, "is_hot": false, "is_cold": true, "should_inline": false}
  ],
  "branches": [
  ],
  "loops": [
    {"location": "hot_loop_a:7", "invocation_count": 2, "total_iterations": 6000, "avg_iterations": 3000.00, "should_unroll": false, "suggested_unroll_factor": 1},
    {"location": "hot_loop_b:15", "invocation_count": 2, "total_iterations": 6000, "avg_iterations": 3000.00, "should_unroll": false, "suggested_unroll_factor": 1}
  ]
}
