add_executable(tst_smoke tst_smoke.cpp)
target_link_libraries(tst_smoke PRIVATE NetCore::NetworkLib Qt5::Test)
add_test(NAME tst_smoke COMMAND tst_smoke)

add_executable(tst_httprequest tst_httprequest.cpp)
target_link_libraries(tst_httprequest PRIVATE NetCore::NetworkLib Qt5::Test)
add_test(NAME tst_httprequest COMMAND tst_httprequest)

add_executable(tst_jsonserializer tst_jsonserializer.cpp)
target_link_libraries(tst_jsonserializer PRIVATE NetCore::NetworkLib Qt5::Test)
add_test(NAME tst_jsonserializer COMMAND tst_jsonserializer)