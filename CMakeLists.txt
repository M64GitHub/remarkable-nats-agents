cmake_minimum_required(VERSION 3.16)
project(hello_remarkable VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Same CMakeLists works for a desktop build (Qt6 from ~/Qt or the system) and a
# cross build (Qt6 from the reMarkable SDK sysroot once its env file is sourced).
# QtNetwork backs the NATS-over-QTcpSocket transport; QtQuickControls2 is NOT
# used — the UI is hand-rolled flat QtQuick for full control over e-paper render.
find_package(Qt6 REQUIRED COMPONENTS Quick Network)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(hello_remarkable
    src/main.cpp
    src/nats/INatsConnection.h   # header-only QObject: listed so AUTOMOC processes it
    src/nats/NatsClient.cpp
    src/agents/AgentProtocol.cpp
    src/agents/AgentModel.cpp
    src/agents/ChatModel.cpp
    src/agents/AppController.cpp)

# A `pragma Singleton` QML file must also be flagged here, or qt_add_qml_module
# registers it as a plain type and `Theme.<prop>` resolves to undefined at runtime.
set_source_files_properties(src/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(hello_remarkable
    URI AgentChat
    VERSION 1.0
    QML_FILES
        src/qml/Main.qml
        src/qml/Theme.qml
        src/qml/FlatButton.qml
        src/qml/RosterView.qml
        src/qml/AgentDelegate.qml
        src/qml/ChatView.qml
        src/qml/MessageDelegate.qml
    RESOURCES
        config/agents.example.json)

target_include_directories(hello_remarkable PRIVATE src)
target_link_libraries(hello_remarkable PRIVATE Qt6::Quick Qt6::Network)
