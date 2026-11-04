if(EXISTS "${SOURCE}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SOURCE}" "${DEST}" COMMAND_ERROR_IS_FATAL ANY)
endif()
