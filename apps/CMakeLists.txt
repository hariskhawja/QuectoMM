add_executable(qmm_replay
    replay_main.cpp
)

target_link_libraries(qmm_replay PRIVATE qmm_engine)
