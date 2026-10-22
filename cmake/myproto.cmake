add_subdirectory(3rdparty/simple-protobuf)

# The generator executes during the build, including macOS cross builds.
if(APPLE)
    set_property(TARGET spb-protoc PROPERTY OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()

spb_protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS core/gen/libcore.proto)

add_library(myproto STATIC ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(myproto
        PUBLIC
        spb-proto
        )
