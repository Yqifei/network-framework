add_executable(tst_smoke tst_smoke.cpp)
target_link_libraries(tst_smoke PRIVATE NetCore::NetworkLib Qt5::Test)
add_test(NAME tst_smoke COMMAND tst_smoke)
