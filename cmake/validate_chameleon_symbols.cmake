if(NOT DEFINED NM_EXECUTABLE)
	message(FATAL_ERROR "NM_EXECUTABLE is required")
endif()

if(NOT DEFINED INPUT_LIB)
	message(FATAL_ERROR "INPUT_LIB is required")
endif()

if(NOT EXISTS "${INPUT_LIB}")
	message(FATAL_ERROR "Library does not exist: ${INPUT_LIB}")
endif()

execute_process(
	COMMAND "${NM_EXECUTABLE}" -D --undefined-only "${INPUT_LIB}"
	RESULT_VARIABLE nm_result
	OUTPUT_VARIABLE nm_output
	ERROR_VARIABLE nm_error
)

if(NOT nm_result EQUAL 0)
	message(FATAL_ERROR "Failed to inspect ${INPUT_LIB} with ${NM_EXECUTABLE}: ${nm_error}")
endif()

string(REPLACE "\n" ";" nm_lines "${nm_output}")
set(unresolved_vulkan_symbols)

foreach(line IN LISTS nm_lines)
	if(line MATCHES "[ \t]U[ \t]+(vk[^ @\t\r\n]+)")
		list(APPEND unresolved_vulkan_symbols "${CMAKE_MATCH_1}")
	endif()
endforeach()

list(REMOVE_DUPLICATES unresolved_vulkan_symbols)

if(unresolved_vulkan_symbols)
	list(JOIN unresolved_vulkan_symbols "\n  " unresolved_vulkan_symbols_text)
	message(FATAL_ERROR
		"Unresolved Vulkan entry points found in ${INPUT_LIB}:\n"
		"  ${unresolved_vulkan_symbols_text}\n"
		"Generated dispatch exposed commands that Chameleon does not implement."
	)
endif()
