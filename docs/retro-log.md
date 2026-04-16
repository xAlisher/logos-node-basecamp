# Retro Log — logos-node-basecamp

Raw wins/fails captured during work. Synthesized at `/retro` after each epic merge.

---

## 2026-04-16

WIN: Full scaffold + tests built and installed first attempt. FakeNodeServer HTTP integration tests passing. Module appeared in sidebar, opened, detected external node, saved config — all working.

WIN: AUTOMOC trick — explicitly listing `${LOGOS_CPP_SDK}/include/cpp/logos_api.h` as a test source file causes AUTOMOC to generate moc_logos_api.cpp, providing LogosAPI::staticMetaObject without linking liblogos_sdk.a. Same pattern as stash-basecamp.

FAIL: qt_policy() not available in this CMake/Qt version — removed, no impact.

FAIL: Listed Node_sidebar.png in manifests before checking actual icon filename. Icon was node.png. Always check assets/icons/ first.
