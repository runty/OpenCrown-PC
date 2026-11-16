; Isolated QA wrapper. Retains the original trace-entry callback exactly once.
define ptr @sub_20619220(ptr noalias %state, i64 %program_counter, ptr noalias %memory) uwtable {
entry:
  call void @dcp_trace_enter(ptr %state, i64 %program_counter)
  %result = call ptr @scale_prototype(ptr %state, i64 %program_counter, ptr %memory)
  ret ptr %result
}
declare void @dcp_trace_enter(ptr, i64)
declare ptr @scale_prototype(ptr, i64, ptr)
