add_executable(tst_smoke tst_smoke.cpp)
target_link_libraries(tst_smoke PRIVATE NetCore::NetworkLib Qt5::Test)
add_test(NAME tst_smoke COMMAND tst_smoke)

add_executable(tst_httprequest tst_httprequest.cpp)
target_link_libraries(tst_httprequest PRIVATE NetCore::NetworkLib Qt5::Test)
add_test(NAME tst_httprequest COMMAND tst_httprequest)

add_executable(tst_jsonserializer tst_jsonserializer.cpp)
target_link_libraries(tst_jsonserializer PRIVATE NetCore::NetworkLib Qt5::Test)
add_test(NAME tst_jsonserializer COMMAND tst_jsonserializer)

add_executable(tst_jsonserializer_robustness tst_jsonserializer_robustness.cpp)
target_link_libraries(tst_jsonserializer_robustness PRIVATE NetCore::NetworkLib Qt5::Test)
add_test(NAME tst_jsonserializer_robustness COMMAND tst_jsonserializer_robustness)

add_executable(tst_requestconverter tst_requestconverter.cpp)
target_link_libraries(tst_requestconverter PRIVATE NetCore::NetworkLib Qt5::Test)
add_test(NAME tst_requestconverter COMMAND tst_requestconverter)

add_executable(tst_networkerror tst_networkerror.cpp)
target_link_libraries(tst_networkerror PRIVATE NetCore::NetworkLib Qt5::Test)
add_test(NAME tst_networkerror COMMAND tst_networkerror)

add_executable(tst_lifecycle tst_lifecycle.cpp)
target_link_libraries(tst_lifecycle PRIVATE NetCore::NetworkLib Qt5::Test)
add_test(NAME tst_lifecycle COMMAND tst_lifecycle)

add_executable(tst_interceptor tst_interceptor.cpp)
target_link_libraries(tst_interceptor PRIVATE NetCore::NetworkLib Qt5::Test)
add_test(NAME tst_interceptor COMMAND tst_interceptor)

add_executable(tst_networkclient tst_networkclient.cpp)
target_link_libraries(tst_networkclient PRIVATE NetCore::NetworkLib Qt5::Test)
add_test(NAME tst_networkclient COMMAND tst_networkclient)

add_executable(tst_httpspec tst_httpspec.cpp)
target_link_libraries(tst_httpspec PRIVATE NetCore::NetworkLib Qt5::Test)
add_test(NAME tst_httpspec COMMAND tst_httpspec)

add_executable(tst_networkservice tst_networkservice.cpp)
target_link_libraries(tst_networkservice PRIVATE NetCore::NetworkLib Qt5::Test)
add_test(NAME tst_networkservice COMMAND tst_networkservice)