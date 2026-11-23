#pragma once

namespace rex::ui {
class Window;
class WindowedAppContext;
}  // namespace rex::ui

namespace crackdown {

// Keeps the window title in step with the render resolution while its setting is on (and the window tag). Call once
// the window exists. The frame rate is Any360's frame counter (--any360_fps_overlay, F2).
void AttachFrameRate(rex::ui::Window* window, rex::ui::WindowedAppContext& app_context);
void DetachFrameRate();


}  // namespace crackdown
