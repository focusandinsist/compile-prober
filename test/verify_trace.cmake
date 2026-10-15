if(NOT EXISTS "${TRACE_FILE}")
    message(FATAL_ERROR "Trace file was not generated: ${TRACE_FILE}")
endif()

file(READ "${TRACE_FILE}" TRACE_CONTENT)

string(FIND "${TRACE_CONTENT}" "\"traceEvents\"" EVENTS_POS)
if(EVENTS_POS EQUAL -1)
    message(FATAL_ERROR "Trace output does not contain traceEvents")
endif()

string(FIND "${TRACE_CONTENT}" "\"ph\":\"X\"" COMPLETE_EVENT_POS)
if(COMPLETE_EVENT_POS EQUAL -1)
    message(FATAL_ERROR "Trace output does not contain complete duration events")
endif()

string(FIND "${TRACE_CONTENT}" "\"dur\"" DURATION_POS)
if(DURATION_POS EQUAL -1)
    message(FATAL_ERROR "Trace output does not contain duration values")
endif()

string(FIND "${TRACE_CONTENT}" "\"ph\":\"B\"" BEGIN_EVENT_POS)
if(NOT BEGIN_EVENT_POS EQUAL -1)
    message(FATAL_ERROR "Legacy B/E trace events are still present")
endif()
