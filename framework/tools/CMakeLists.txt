add_executable(echo_schema_export schema_export.cpp)
target_link_libraries(echo_schema_export PRIVATE
    EchoFramework::Data EchoFramework::Inventory EchoFramework::Attributes
    EchoFramework::Equipment EchoFramework::Abilities EchoFramework::Quests
    EchoFramework::Cutscenes)
target_compile_features(echo_schema_export PRIVATE cxx_std_20)
target_compile_definitions(echo_schema_export PRIVATE
    ECHO_MANIFEST_HAS_INVENTORY ECHO_MANIFEST_HAS_ATTRIBUTES
    ECHO_MANIFEST_HAS_EQUIPMENT ECHO_MANIFEST_HAS_ABILITIES
    ECHO_MANIFEST_HAS_QUESTS ECHO_MANIFEST_HAS_CUTSCENES)
if(TARGET EchoFramework::Narrative)
    target_link_libraries(echo_schema_export PRIVATE EchoFramework::Narrative)
    target_compile_definitions(echo_schema_export PRIVATE ECHO_MANIFEST_HAS_NARRATIVE)
endif()

add_executable(echo_content_validate content_validate.cpp)
target_link_libraries(echo_content_validate PRIVATE EchoFramework::Data)
target_compile_features(echo_content_validate PRIVATE cxx_std_20)

add_executable(echo_component_editor component_editor.cpp)
target_link_libraries(echo_component_editor PRIVATE EchoFramework::Data)
target_compile_features(echo_component_editor PRIVATE cxx_std_20)

if(ECHO_FRAMEWORK_BUILD_TESTS)
    add_executable(echo_manifest_input_tests capability_manifest_input_tests.cpp)
    target_link_libraries(echo_manifest_input_tests PRIVATE EchoFramework::Data)
    target_compile_features(echo_manifest_input_tests PRIVATE cxx_std_20)
    add_test(NAME echo_framework_manifest_input COMMAND echo_manifest_input_tests)
endif()
