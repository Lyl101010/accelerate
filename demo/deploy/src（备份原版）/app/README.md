# app

Application orchestration layer.

Future files here should own:

- command-line or interactive command handling
- startup and shutdown order
- module initialization and release
- the main pipeline that calls input, detection, risk, VLM, upload, and telemetry

Target shape:

```cpp
int main() {
    App app;
    return app.run();
}
```
