if(NOT DEFINED NM_EXECUTABLE)
	message(FATAL_ERROR "NM_EXECUTABLE is required")
endif()

if(NOT DEFINED INPUT_LIB OR NOT EXISTS "${INPUT_LIB}")
	message(FATAL_ERROR "INPUT_LIB must name an existing library")
endif()

execute_process(
	COMMAND "${NM_EXECUTABLE}" -D --defined-only "${INPUT_LIB}"
	RESULT_VARIABLE defined_result
	OUTPUT_VARIABLE defined_output
	ERROR_VARIABLE defined_error
)
if(NOT defined_result EQUAL 0)
	message(FATAL_ERROR "Failed to inspect ${INPUT_LIB}: ${defined_error}")
endif()

foreach(required_symbol clIcdGetPlatformIDsKHR clGetPlatformInfo clGetExtensionFunctionAddress)
	if(NOT defined_output MATCHES "[ 	]${required_symbol}([\r\n]|$)")
		message(FATAL_ERROR "Required OpenCL ICD symbol is not exported: ${required_symbol}")
	endif()
endforeach()

execute_process(
	COMMAND "${NM_EXECUTABLE}" -D --undefined-only "${INPUT_LIB}"
	RESULT_VARIABLE undefined_result
	OUTPUT_VARIABLE undefined_output
	ERROR_VARIABLE undefined_error
)
if(NOT undefined_result EQUAL 0)
	message(FATAL_ERROR "Failed to inspect ${INPUT_LIB}: ${undefined_error}")
endif()

if(undefined_output MATCHES "[ 	]U[ 	]+(cl[A-Za-z0-9_]+)")
	message(FATAL_ERROR "Unresolved OpenCL entry point in ${INPUT_LIB}: ${CMAKE_MATCH_1}")
endif()
