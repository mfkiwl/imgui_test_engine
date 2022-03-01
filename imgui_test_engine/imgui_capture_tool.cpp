// Dear ImGui Screen/Video Capture Tool
// This is usable as a standalone applet or controlled by the test engine.
// (code)

// Two mode of operation:
// - Interactive: call ImGuiCaptureToolUI::ShowCaptureToolWindow()
// - Programmatic: generally via ImGuiTestContext::CaptureXXX functions

// FIXME: This probably needs a rewrite, it's a bit too complicated.

/*

Index of this file:

// [SECTION] Includes
// [SECTION] ImGuiCaptureImageBuf
// [SECTION] ImGuiCaptureContext
// [SECTION] ImGuiCaptureToolUI

*/

//-----------------------------------------------------------------------------
// [SECTION] Includes
//-----------------------------------------------------------------------------

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "imgui.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"
#include "imgui_capture_tool.h"
#include "imgui_te_utils.h"         // ImPathFindFilename, ImPathFindExtension, ImPathFixSeparatorsForCurrentOS, ImFileCreateDirectoryChain, ImOsOpenInShell
#include "thirdparty/Str/Str.h"

//-----------------------------------------------------------------------------
// [SECTION] Link stb_image_write.h
//-----------------------------------------------------------------------------

#if IMGUI_TEST_ENGINE_ENABLE_CAPTURE

// Compile time options:
//#define IMGUI_STB_NAMESPACE            ImStb
//#define IMGUI_STB_IMAGE_WRITE_FILENAME "my_folder/stb_image_write.h"
//#define IMGUI_DISABLE_STB_IMAGE_WRITE_IMPLEMENTATION

// stb_image_write
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable: 4456)                             // declaration of 'xx' hides previous local declaration
#pragma warning (disable: 4457)                             // declaration of 'xx' hides function parameter
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif

#ifdef IMGUI_STB_NAMESPACE
namespace IMGUI_STB_NAMESPACE
{
#endif
#ifndef STB_IMAGE_WRITE_IMPLEMENTATION                       // in case the user already have an implementation in the _same_ compilation unit (e.g. unity builds)
#ifndef IMGUI_DISABLE_STB_IMAGE_WRITE_IMPLEMENTATION         // in case the user already have an implementation in another compilation unit
#define STB_IMAGE_WRITE_IMPLEMENTATION
#endif
#ifdef IMGUI_STB_IMAGE_WRITE_FILENAME
#include IMGUI_STB_IMAGE_WRITE_FILENAME
#else
#include "thirdparty/stb/imstb_image_write.h"
#endif  // #ifdef IMGUI_STB_IMAGE_WRITE_FILENAME
#endif  // #ifndef STB_IMAGE_WRITE_IMPLEMENTATION
#ifdef IMGUI_STB_NAMESPACE
} // namespace ImStb
using namespace IMGUI_STB_NAMESPACE;
#endif

#ifdef _MSC_VER
#pragma warning (pop)
#else
#pragma GCC diagnostic pop
#endif

#endif // #if IMGUI_TEST_ENGINE_ENABLE_CAPTURE

//-----------------------------------------------------------------------------
// [SECTION] ImGuiCaptureImageBuf
// Helper class for simple bitmap manipulation (not particularly efficient!)
//-----------------------------------------------------------------------------

void ImGuiCaptureImageBuf::Clear()
{
    if (Data)
        IM_FREE(Data);
    Data = NULL;
}

void ImGuiCaptureImageBuf::CreateEmpty(int w, int h)
{
    Clear();
    Width = w;
    Height = h;
    Data = (unsigned int*)IM_ALLOC((size_t)(Width * Height * 4));
    memset(Data, 0, (size_t)(Width * Height * 4));
}

bool ImGuiCaptureImageBuf::SaveFile(const char* filename)
{
#if IMGUI_TEST_ENGINE_ENABLE_CAPTURE
    IM_ASSERT(Data != NULL);
    int ret = stbi_write_png(filename, Width, Height, 4, Data, Width * 4);
    return ret != 0;
#else
    IM_UNUSED(filename);
    return false;
#endif
}

void ImGuiCaptureImageBuf::RemoveAlpha()
{
    unsigned int* p = Data;
    int n = Width * Height;
    while (n-- > 0)
    {
        *p |= IM_COL32_A_MASK;
        p++;
    }
}

//-----------------------------------------------------------------------------
// [SECTION] ImGuiCaptureContext
//-----------------------------------------------------------------------------

#if IMGUI_TEST_ENGINE_ENABLE_CAPTURE
static void HideOtherWindows(const ImGuiCaptureArgs* args)
{
    ImGuiContext& g = *GImGui;
    for (ImGuiWindow* window : g.Windows)
    {
        if (window->Flags & ImGuiWindowFlags_ChildWindow)
            continue;
        if ((window->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip)) != 0 && (args->InFlags & ImGuiCaptureFlags_IncludeTooltipsAndPopups) != 0)
            continue;
        if (args->InCaptureWindows.contains(window))
            continue;

#ifdef IMGUI_HAS_DOCK
        bool should_hide_window = true;
        if ((window->Flags & ImGuiWindowFlags_DockNodeHost))
            for (ImGuiWindow* capture_window : args->InCaptureWindows)
            {
                if (capture_window->DockNode != NULL && capture_window->DockNode->HostWindow == window)
                {
                    should_hide_window = false;
                    break;
                }
            }
        if (!should_hide_window)
            continue;
#endif  // IMGUI_HAS_DOCK

        // Not overwriting HiddenFramesCanSkipItems or HiddenFramesCannotSkipItems since they have side-effects (e.g. preserving ContentsSize)
        if (window->WasActive || window->Active)
            window->HiddenFramesForRenderOnly = 2;
    }
}
#endif  // IMGUI_TEST_ENGINE_ENABLE_CAPTURE

static ImRect GetMainViewportRect()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    return ImRect(viewport->Pos, viewport->Pos + viewport->Size);
}

void ImGuiCaptureContext::PostNewFrame()
{
    const ImGuiCaptureArgs* args = _CaptureArgs;
    if (args == NULL)
        return;

    ImGuiContext& g = *GImGui;

    // Override mouse cursor
    // FIXME: Could override in Pre+Post Render() hooks to avoid doing a backup.
    if (args->InFlags & ImGuiCaptureFlags_HideMouseCursor)
    {
        if (_FrameNo == 1)
            _BackupMouseDrawCursor = g.IO.MouseDrawCursor;
        g.IO.MouseDrawCursor = false;
    }

    // Force mouse position. Hovered window is reset in ImGui::NewFrame() based on mouse real mouse position.
    // FIXME: Would be saner to override io.MousePos in Pre NewFrame() hook.
    if (_FrameNo > 2 && (args->InFlags & ImGuiCaptureFlags_StitchAll) != 0)
    {
        IM_ASSERT(args->InCaptureWindows.Size == 1);
        g.IO.MousePos = args->InCaptureWindows[0]->Pos + _MouseRelativeToWindowPos;
        g.HoveredWindow = _HoveredWindow;
    }
}

// Returns true when capture is in progress.
ImGuiCaptureStatus ImGuiCaptureContext::CaptureUpdate(ImGuiCaptureArgs* args)
{
#if IMGUI_TEST_ENGINE_ENABLE_CAPTURE
    ImGuiContext& g = *GImGui;
    ImGuiIO& io = g.IO;

    // Sanity checks
    IM_ASSERT(args != NULL);
    IM_ASSERT(ScreenCaptureFunc != NULL);
    IM_ASSERT(args->InOutputImageBuf != NULL || args->InOutputFile[0]);
    IM_ASSERT(args->InRecordFPSTarget != 0);
    if (_VideoRecording)
    {
        IM_ASSERT(args->InOutputFile[0] && "Output filename must be specified when recording videos.");
        IM_ASSERT(args->InOutputImageBuf == NULL && "Output buffer cannot be specified when recording videos.");
        IM_ASSERT((args->InFlags & ImGuiCaptureFlags_StitchAll) == 0 && "Image stitching is not supported when recording videos.");
    }

    ImGuiCaptureImageBuf* output = args->InOutputImageBuf ? args->InOutputImageBuf : &_CaptureBuf;
    const ImRect viewport_rect = GetMainViewportRect();

    // Hide other windows so they can't be seen visible behind captured window
    if ((args->InFlags & ImGuiCaptureflags_IncludeOtherWindows) == 0 && !args->InCaptureWindows.empty())
        HideOtherWindows(args);

    // Recording will be set to false when we are stopping video capture.
    const bool is_recording_video = IsCapturingVideo();
    const double current_time_sec = ImGui::GetTime();
    if (is_recording_video && _VideoLastFrameTime > 0)
    {
        double delta_sec = current_time_sec - _VideoLastFrameTime;
        if (delta_sec < 1.0 / args->InRecordFPSTarget)
            return ImGuiCaptureStatus_InProgress;
    }

    // Capture can be performed in single frame if we are capturing a rect.
    const bool instant_capture = (args->InFlags & ImGuiCaptureFlags_Instant) != 0;
    const bool is_capturing_explicit_rect = args->InCaptureRect.GetWidth() > 0 && args->InCaptureRect.GetHeight() > 0;
    if (instant_capture)
    {
        IM_ASSERT(args->InCaptureWindows.empty());
        IM_ASSERT(is_capturing_explicit_rect);
        IM_ASSERT(is_recording_video == false);
        IM_ASSERT((args->InFlags & ImGuiCaptureFlags_StitchAll) == 0);
    }

    //-----------------------------------------------------------------
    // Frame 0: Initialize capture state
    //-----------------------------------------------------------------
    if (_FrameNo == 0)
    {
        if (is_recording_video)
        {
            // Determinate size alignment
            const char* extension = (char*)ImPathFindExtension(args->InOutputFile);
            if (args->InSizeAlign == 0)
            {
                if (strcmp(extension, ".gif") == 0)
                    args->InSizeAlign = 1;
                else
                    args->InSizeAlign = 2; // mp4 wants >= 2
            }
            IM_ASSERT(args->InSizeAlign > 0);
        }

        // When recording, same args should have been passed to BeginVideoCapture().
        IM_ASSERT(!_VideoRecording || _CaptureArgs == args);

        _CaptureArgs = args;
        _ChunkNo = 0;
        _CaptureRect = _CapturedWindowRect = ImRect(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
        _BackupWindows.clear();
        _BackupWindowsRect.clear();
        _BackupDisplayWindowPadding = g.Style.DisplayWindowPadding;
        _BackupDisplaySafeAreaPadding = g.Style.DisplaySafeAreaPadding;
        g.Style.DisplayWindowPadding = ImVec2(0, 0);    // Allow windows to be positioned fully outside of visible viewport.
        g.Style.DisplaySafeAreaPadding = ImVec2(0, 0);

        if (is_capturing_explicit_rect)
        {
            // Capture arbitrary rectangle. If any windows are specified in this mode only they will appear in captured region.
            _CaptureRect = args->InCaptureRect;
            if (args->InCaptureWindows.empty() && !instant_capture)
            {
                // Gather all top level windows. We will need to move them in order to capture regions larger than viewport.
                for (ImGuiWindow* window : g.Windows)
                {
                    // Child windows will be included by their parents.
                    if (window->ParentWindow != NULL)
                        continue;
                    if ((window->Flags & ImGuiWindowFlags_Popup || window->Flags & ImGuiWindowFlags_Tooltip) && !(args->InFlags & ImGuiCaptureFlags_IncludeTooltipsAndPopups))
                        continue;
                    args->InCaptureWindows.push_back(window);
                }
            }
        }

        // Save rectangle covering all windows and find top-left corner of combined rect which will be used to
        // translate this group of windows to top-left corner of the screen.
        for (ImGuiWindow* window : args->InCaptureWindows)
        {
            _CapturedWindowRect.Add(window->Rect());
            _BackupWindows.push_back(window);
            _BackupWindowsRect.push_back(window->Rect());
        }

        if (args->InFlags & ImGuiCaptureFlags_StitchAll)
        {
            IM_ASSERT(is_capturing_explicit_rect == false && "ImGuiCaptureContext: capture of full window contents is not possible when capturing specified rect.");
            IM_ASSERT(args->InCaptureWindows.Size == 1 && "ImGuiCaptureContext: capture of full window contents is not possible when capturing more than one window.");

            // Resize window to it's contents and capture it's entire width/height. However if window is bigger than it's contents - keep original size.
            ImGuiWindow* window = args->InCaptureWindows[0];
            ImVec2 full_size = window->SizeFull;

            // Mouse cursor is relative to captured window even if it is not hovered, in which case cursor is kept off the window to prevent appearing in screenshot multiple times by accident.
            _MouseRelativeToWindowPos = io.MousePos - window->Pos + window->Scroll;

            // FIXME-CAPTURE: Window width change may affect vertical content size if window contains text that wraps. To accurately position mouse cursor for capture we avoid horizontal resize.
            // Instead window width should be set manually before capture, as it is simple to do and most of the time we already have a window of desired width.
            //full_size.x = ImMax(window->SizeFull.x, window->ContentSize.x + (window->WindowPadding.x + window->WindowBorderSize) * 2);
            full_size.y = ImMax(window->SizeFull.y, window->ContentSize.y + (window->WindowPadding.y + window->WindowBorderSize) * 2 + window->TitleBarHeight() + window->MenuBarHeight());
            ImGui::SetWindowSize(window, full_size);
            _HoveredWindow = g.HoveredWindow;
        }
        else
        {
            _MouseRelativeToWindowPos = ImVec2(-FLT_MAX, -FLT_MAX);
            _HoveredWindow = NULL;
        }
    }
    else
    {
        IM_ASSERT(args == _CaptureArgs); // Capture args can not change mid-capture.
    }

    //-----------------------------------------------------------------
    // Frame 1: Skipped to allow window size to update fully
    //-----------------------------------------------------------------

    //-----------------------------------------------------------------
    // Frame 2: Position windows, lock rectangle, create capture buffer
    //-----------------------------------------------------------------
    if (_FrameNo == 2 || instant_capture)
    {
        // Move group of windows so combined rectangle position is at the top-left corner + padding and create combined
        // capture rect of entire area that will be saved to screenshot. Doing this on the second frame because when
        // ImGuiCaptureFlags_StitchAll flag is used we need to allow window to reposition.
        // Repositioning of a window may take multiple frames, depending on whether window was already rendered or not.
        if (args->InFlags & ImGuiCaptureFlags_StitchAll)
        {
            ImVec2 move_offset = ImVec2(args->InPadding, args->InPadding) - _CapturedWindowRect.Min + viewport_rect.Min;
            for (ImGuiWindow* window : args->InCaptureWindows) // FIXME: Technically with ImGuiCaptureFlags_StitchAll we are dealing with a single window, so loop is misleading
                ImGui::SetWindowPos(window, window->Pos + move_offset);
        }

        // Determine capture rectangle if not provided by user
        if (!is_capturing_explicit_rect)
        {
            if (args->InCaptureWindows.Size > 0)
            {
                for (ImGuiWindow* window : args->InCaptureWindows)
                    _CaptureRect.Add(window->Rect());
                _CaptureRect.Expand(args->InPadding);
            }
            else
            {
                _CaptureRect = viewport_rect;
            }
        }
        if ((args->InFlags & ImGuiCaptureFlags_StitchAll) == 0)
        {
            // Can not capture area outside of screen. Clip capture rect, since we are capturing only visible rect anyway.
            _CaptureRect.ClipWith(viewport_rect);

            // Align size
            // FIXME: ffmpeg + codec can possibly handle that better on their side.
            ImVec2 capture_size_aligned = _CaptureRect.GetSize();
            if (args->InSizeAlign > 1)
            {
                // Round up
                IM_ASSERT(ImIsPowerOfTwo(args->InSizeAlign));
                capture_size_aligned.x = (float)IM_MEMALIGN((int)capture_size_aligned.x, args->InSizeAlign);
                capture_size_aligned.y = (float)IM_MEMALIGN((int)capture_size_aligned.y, args->InSizeAlign);

                // Unless will stray off viewport, then round down
                if (_CaptureRect.Min.x + capture_size_aligned.x >= viewport_rect.Max.x)
                    capture_size_aligned.x -= args->InSizeAlign;
                if (_CaptureRect.Min.y + capture_size_aligned.y >= viewport_rect.Max.y)
                    capture_size_aligned.y -= args->InSizeAlign;

                IM_ASSERT(capture_size_aligned.x > 0);
                IM_ASSERT(capture_size_aligned.y > 0);
                _CaptureRect.Max = _CaptureRect.Min + capture_size_aligned;
            }
        }

        // Initialize capture buffer.
        IM_ASSERT(!_CaptureRect.IsInverted());
        args->OutImageSize = _CaptureRect.GetSize();
        output->CreateEmpty((int)_CaptureRect.GetWidth(), (int)_CaptureRect.GetHeight());
    }

    //-----------------------------------------------------------------
    // Frame 4+N*4: Capture a frame
    //-----------------------------------------------------------------
    if (((_FrameNo > 2) && (_FrameNo % 4) == 0) || (is_recording_video && _FrameNo > 2) || instant_capture)
    {
        // FIXME: Implement capture of regions wider than viewport.
        // Capture a portion of image. Capturing of windows wider than viewport is not implemented yet.
        const ImRect clip_rect = viewport_rect;
        ImRect capture_rect = _CaptureRect;
        capture_rect.ClipWith(clip_rect);
        const int capture_height = ImMin((int)io.DisplaySize.y, (int)_CaptureRect.GetHeight());
        const int x1 = (int)(capture_rect.Min.x - clip_rect.Min.x);
        const int y1 = (int)(capture_rect.Min.y - clip_rect.Min.y);
        const int w = (int)capture_rect.GetWidth();
        const int h = (int)ImMin(output->Height - _ChunkNo * capture_height, capture_height);
        if (h > 0)
        {
            IM_ASSERT(w == output->Width);
            if (args->InFlags & ImGuiCaptureFlags_StitchAll)
                IM_ASSERT(h <= output->Height);     // When stitching, image can be taller than captured viewport.
            else
                IM_ASSERT(h == output->Height);

            const ImGuiID viewport_id = 0;
            if (!ScreenCaptureFunc(viewport_id, x1, y1, w, h, &output->Data[_ChunkNo * w * capture_height], ScreenCaptureUserData))
            {
                fprintf(stderr, "Screen capture function failed.\n");
                return ImGuiCaptureStatus_Error;
            }

            if (args->InFlags & ImGuiCaptureFlags_StitchAll)
            {
                // Window moves up in order to expose it's lower part.
                for (ImGuiWindow* window : args->InCaptureWindows)
                    ImGui::SetWindowPos(window, window->Pos - ImVec2(0, (float)h));
                _CaptureRect.TranslateY(-(float)h);
                _ChunkNo++;
            }

            if (is_recording_video && (args->InFlags & ImGuiCaptureFlags_NoSave) == 0)
            {
                // _FFMPEGStdIn is NULL when recording just started. Initialize recording state.
                if (_VideoFFMPEGPipe == NULL)
                {
                    // First video frame, initialize now that dimensions are known.
                    const unsigned int width = (unsigned int)capture_rect.GetWidth();
                    const unsigned int height = (unsigned int)capture_rect.GetHeight();
                    IM_ASSERT(VideoCapturePathToFFMPEG != NULL && VideoCapturePathToFFMPEG[0]);
                    Str64f ffmpeg_exe(VideoCapturePathToFFMPEG);
                    ImPathFixSeparatorsForCurrentOS(ffmpeg_exe.c_str());
                    Str256f cmd("");
                    cmd.setf(
                        "\"%s\" -r %d -f rawvideo -pix_fmt rgba -s %dx%d -i - -threads 0 -vsync 0 "
                        "-preset ultrafast -y -pix_fmt yuv420p -crf %d -hide_banner -loglevel error "
                        "%s",
                        ffmpeg_exe.c_str(), args->InRecordFPSTarget, width, height, args->InRecordQuality, args->InOutputFile);
                    fprintf(stdout, "# %s\n", cmd.c_str());
                    _VideoFFMPEGPipe = ImOsPOpen(cmd.c_str(), "w");
                    IM_ASSERT(_VideoFFMPEGPipe != NULL);
                }

                // Save new video frame
                fwrite(output->Data, 1, output->Width * output->Height * 4, _VideoFFMPEGPipe);
            }
            if (is_recording_video)
                _VideoLastFrameTime = current_time_sec;
        }

        // Image is finalized immediately when we are not stitching. Otherwise image is finalized when we have captured and stitched all frames.
        if (!_VideoRecording && (!(args->InFlags & ImGuiCaptureFlags_StitchAll) || h <= 0))
        {
            output->RemoveAlpha();

            if (_VideoFFMPEGPipe != NULL)
            {
                // At this point _Recording is false, but we know we were recording because _FFMPEGStdIn is not NULL. Finalize video here.
                ImOsPClose(_VideoFFMPEGPipe);
                _VideoFFMPEGPipe = NULL;
            }
            else if (args->InOutputImageBuf == NULL)
            {
                // Save single frame.
                if ((args->InFlags & ImGuiCaptureFlags_NoSave) == 0)
                    output->SaveFile(args->InOutputFile);
                output->Clear();
            }

            // Restore window positions unconditionally. We may have moved them ourselves during capture.
            for (int i = 0; i < _BackupWindows.Size; i++)
            {
                ImGuiWindow* window = _BackupWindows[i];
                if (window->Hidden)
                    continue;
                ImGui::SetWindowPos(window, _BackupWindowsRect[i].Min, ImGuiCond_Always);
                ImGui::SetWindowSize(window, _BackupWindowsRect[i].GetSize(), ImGuiCond_Always);
            }
            if (args->InFlags & ImGuiCaptureFlags_HideMouseCursor)
                g.IO.MouseDrawCursor = _BackupMouseDrawCursor;
            g.Style.DisplayWindowPadding = _BackupDisplayWindowPadding;
            g.Style.DisplaySafeAreaPadding = _BackupDisplaySafeAreaPadding;

            _FrameNo = _ChunkNo = 0;
            _VideoLastFrameTime = 0;
            _MouseRelativeToWindowPos = ImVec2(-FLT_MAX, -FLT_MAX);
            _HoveredWindow = NULL;
            _CaptureArgs = NULL;

            return ImGuiCaptureStatus_Done;
        }
    }

    // Keep going
    _FrameNo++;
    return ImGuiCaptureStatus_InProgress;
#else
    IM_UNUSED(args);
    return ImGuiCaptureStatus_Done;
#endif
}

void ImGuiCaptureContext::BeginVideoCapture(ImGuiCaptureArgs* args)
{
    IM_ASSERT(args != NULL);
    IM_ASSERT(_VideoRecording == false);
    IM_ASSERT(_VideoFFMPEGPipe == NULL);
    IM_ASSERT(args->InRecordFPSTarget >= 1 && args->InRecordFPSTarget <= 100);

    _VideoRecording = true;
    _CaptureArgs = args;
}

void ImGuiCaptureContext::EndVideoCapture()
{
    IM_ASSERT(_CaptureArgs != NULL);
    IM_ASSERT(_VideoRecording == true);

    _VideoRecording = false;
}

bool ImGuiCaptureContext::IsCapturingVideo()
{
    return _VideoRecording;
}

//-----------------------------------------------------------------------------
// ImGuiCaptureToolUI
//-----------------------------------------------------------------------------

ImGuiCaptureToolUI::ImGuiCaptureToolUI()
{
    // Filename template for where screenshots will be saved. May contain directories or variation of %d format.
    strcpy(_OutputFileTemplate, "output/captures/imgui_capture_%04d.png");
}

// Interactively pick a single window
void ImGuiCaptureToolUI::CaptureWindowPicker(ImGuiCaptureArgs* args)
{
    ImGuiContext& g = *GImGui;
    ImGuiIO& io = g.IO;

    const float TEXT_BASE_WIDTH = ImGui::CalcTextSize("A").x;
    const ImVec2 button_sz = ImVec2(TEXT_BASE_WIDTH * 30, 0.0f);
    const ImGuiID picking_id = ImGui::GetID("##picking");

    if (ImGui::Button("Capture Single Window..", button_sz))
        _StateIsPickingWindow = true;

    if (_StateIsPickingWindow)
    {
        // Picking a window
        ImGuiWindow* capture_window = g.HoveredWindow ? g.HoveredWindow->RootWindow : NULL;
        ImDrawList* fg_draw_list = ImGui::GetForegroundDrawList();
        ImGui::SetActiveID(picking_id, g.CurrentWindow);    // Steal active ID so our click won't interact with something else.
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("Capture window: '%s'\nPress ESC to cancel.", capture_window ? capture_window->Name : "<None>");

        // FIXME: Would be nice to have a way to enforce front-most windows. Perhaps make this Render() feature more generic.
        //if (capture_window)
        //    g.NavWindowingTarget = capture_window;

        // Draw rect that is about to be captured
        const ImRect viewport_rect = GetMainViewportRect();
        const ImU32 col_dim_overlay = IM_COL32(0, 0, 0, 40);
        if (capture_window)
        {
            ImRect r = capture_window->Rect();
            r.Expand(args->InPadding);
            r.ClipWith(ImRect(ImVec2(0, 0), io.DisplaySize));
            r.Expand(1.0f);
            fg_draw_list->AddRect(r.Min, r.Max, IM_COL32_WHITE, 0.0f, 0, 2.0f);
            ImGui::RenderRectFilledWithHole(fg_draw_list, viewport_rect, r, col_dim_overlay, 0.0f);
        }
        else
        {
            fg_draw_list->AddRectFilled(viewport_rect.Min, viewport_rect.Max, col_dim_overlay);
        }

        if (ImGui::IsMouseClicked(0) && capture_window)
        {
            ImGui::FocusWindow(capture_window);
            _SelectedWindows.resize(0);
            _StateIsPickingWindow = false;
            _StateIsCapturing = true;
            args->InCaptureWindows.clear();
            args->InCaptureWindows.push_back(capture_window);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            _StateIsPickingWindow = _StateIsCapturing = false;
    }
    else
    {
        if (ImGui::GetActiveID() == picking_id)
            ImGui::ClearActiveID();
    }
}

void ImGuiCaptureToolUI::CaptureWindowsSelector(ImGuiCaptureArgs* args)
{
    ImGuiContext& g = *GImGui;
    ImGuiIO& io = g.IO;

    // Gather selected windows
    ImRect capture_rect(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (ImGuiWindow* window : g.Windows)
    {
        if (window->WasActive == false)
            continue;
        if (window->Flags & ImGuiWindowFlags_ChildWindow)
            continue;
        const bool is_popup = (window->Flags & ImGuiWindowFlags_Popup) || (window->Flags & ImGuiWindowFlags_Tooltip);
        if ((args->InFlags & ImGuiCaptureFlags_IncludeTooltipsAndPopups) && is_popup)
        {
            capture_rect.Add(window->Rect());
            args->InCaptureWindows.push_back(window);
            continue;
        }
        if (is_popup)
            continue;
        if (_SelectedWindows.contains(window->RootWindow->ID))
        {
            capture_rect.Add(window->Rect());
            args->InCaptureWindows.push_back(window);
        }
    }
    const bool allow_capture = !capture_rect.IsInverted() && args->InCaptureWindows.Size > 0 && _OutputFileTemplate[0];

    const float TEXT_BASE_WIDTH = ImGui::CalcTextSize("A").x;
    const ImVec2 button_sz = ImVec2(TEXT_BASE_WIDTH * 30, 0.0f);

    // Capture Multiple Button
    {
        char label[64];
        ImFormatString(label, 64, "Capture Multiple (%d)###CaptureMultiple", args->InCaptureWindows.Size);

        if (!allow_capture)
            ImGui::BeginDisabled();
        bool do_capture = ImGui::Button(label, button_sz);
        do_capture |= io.KeyAlt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_C));
        if (!allow_capture)
            ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Alternatively press Alt+C to capture selection.");
        if (do_capture)
            _StateIsCapturing = true;
    }

    // Record video button
    // (Prefer 100/FPS to be an integer)
    {
        const bool is_capturing_video = Context.IsCapturingVideo();
        if (is_capturing_video)
        {
            if (ImGui::Button("Stop capturing video###CaptureVideo", button_sz))
                Context.EndVideoCapture();
        }
        else
        {
            char label[64];
            ImFormatString(label, 64, "Capture video (%d)###CaptureVideo", args->InCaptureWindows.Size);
            if (!allow_capture)
                ImGui::BeginDisabled();
            if (ImGui::Button(label, button_sz))
            {
                _StateIsCapturing = true;
                Context.BeginVideoCapture(args);
            }
            if (!allow_capture)
                ImGui::EndDisabled();
        }
    }

    // Draw capture rectangle
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    if (allow_capture && !_StateIsPickingWindow && !_StateIsCapturing)
    {
        IM_ASSERT(capture_rect.GetWidth() > 0);
        IM_ASSERT(capture_rect.GetHeight() > 0);
        const ImRect viewport_rect = GetMainViewportRect();
        capture_rect.Expand(args->InPadding);
        capture_rect.ClipWith(viewport_rect);
        draw_list->AddRect(capture_rect.Min - ImVec2(1.0f, 1.0f), capture_rect.Max + ImVec2(1.0f, 1.0f), IM_COL32_WHITE);
    }

    ImGui::Separator();

    // Show window list and update rectangles
    ImGui::Text("Windows:");
    if (ImGui::BeginTable("split", 2))
    {
        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthStretch);
        for (ImGuiWindow* window : g.Windows)
        {
            if (!window->WasActive)
                continue;

            const bool is_popup = (window->Flags & ImGuiWindowFlags_Popup) || (window->Flags & ImGuiWindowFlags_Tooltip);
            if (is_popup)
                continue;

            if (window->Flags & ImGuiWindowFlags_ChildWindow)
                continue;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(window);

            // Ensure that text after the ## is actually displayed to the user (FIXME: won't be able to check/uncheck from that portion of the text)
            bool is_selected = _SelectedWindows.contains(window->RootWindow->ID);
            if (ImGui::Checkbox(window->Name, &is_selected))
            {
                if (is_selected)
                    _SelectedWindows.push_back(window->RootWindow->ID);
                else
                    _SelectedWindows.find_erase_unsorted(window->RootWindow->ID);
            }

            if (const char* remaining_text = ImGui::FindRenderedTextEnd(window->Name))
                if (remaining_text[0] != 0)
                {
                    if (remaining_text > window->Name)
                        ImGui::SameLine(0, 1);
                    else
                        ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
                    ImGui::TextUnformatted(remaining_text);
                }

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(TEXT_BASE_WIDTH * 9.0f);
            ImGui::DragFloat2("Pos", &window->Pos.x, 0.05f, 0.0f, 0.0f, "%.0f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(TEXT_BASE_WIDTH * 9.0f);
            ImGui::DragFloat2("Size", &window->SizeFull.x, 0.05f, 0.0f, 0.0f, "%.0f");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void ImGuiCaptureToolUI::ShowCaptureToolWindow(bool* p_open)
{
    // Update capturing
    if (_StateIsCapturing)
    {
        ImGuiCaptureArgs* args = &_CaptureArgs;
        if (Context.IsCapturingVideo() || args->InCaptureWindows.Size > 1)
            args->InFlags &= ~ImGuiCaptureFlags_StitchAll;

        if (Context._VideoRecording && ImGui::IsKeyPressed(ImGuiKey_Escape))
            Context.EndVideoCapture();

        ImGuiCaptureStatus status = Context.CaptureUpdate(args);
        if (status != ImGuiCaptureStatus_InProgress)
        {
            if (status == ImGuiCaptureStatus_Done)
                ImStrncpy(OutputLastFilename, args->InOutputFile, IM_ARRAYSIZE(OutputLastFilename));
            _StateIsCapturing = false;
            _FileCounter++;
        }
    }

    // Update UI
    if (!ImGui::Begin("Dear ImGui Capture Tool", p_open))
    {
        ImGui::End();
        return;
    }
    if (Context.ScreenCaptureFunc == NULL)
    {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Backend is missing ScreenCaptureFunc!");
        ImGui::End();
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();

    // Options
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::TreeNode("Options"))
    {
        // Open Last
        {
            const bool has_last_file_name = (OutputLastFilename[0] != 0);
            if (!has_last_file_name)
                ImGui::BeginDisabled();
            if (ImGui::Button("Open Last"))
                ImOsOpenInShell(OutputLastFilename);
            if (!has_last_file_name)
                ImGui::EndDisabled();
            if (has_last_file_name && ImGui::IsItemHovered())
                ImGui::SetTooltip("Open %s", OutputLastFilename);
            ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        }

        // Open Directory
        {
            char output_dir[256];
            strcpy(output_dir, _OutputFileTemplate);
            char* output_filename = (char*)ImPathFindFilename(output_dir);
            if (output_filename > output_dir)
                output_filename[-1] = 0;
            else
                strcpy(output_dir, ".");
            if (ImGui::Button("Open Directory"))
            {
                ImPathFixSeparatorsForCurrentOS(output_dir);
                ImOsOpenInShell(output_dir);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Open %s/", output_dir);
        }

        const float TEXT_BASE_WIDTH = ImGui::CalcTextSize("A").x;
        const float BUTTON_WIDTH = (float)(int)-(TEXT_BASE_WIDTH * 26);

        ImGui::PushItemWidth(BUTTON_WIDTH);
        ImGui::InputText("Output template", _OutputFileTemplate, IM_ARRAYSIZE(_OutputFileTemplate));
        ImGui::DragFloat("Padding", &_CaptureArgs.InPadding, 0.1f, 0, 32, "%.0f");
        ImGui::DragInt("Video FPS", &_CaptureArgs.InRecordFPSTarget, 0.1f, 10, 100, "%d fps");

        if (ImGui::Button("Snap Windows To Grid", ImVec2(BUTTON_WIDTH, 0)))
            SnapWindowsToGrid(SnapGridSize);
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::SetNextItemWidth((float)(int)-(TEXT_BASE_WIDTH * 5));
        ImGui::DragFloat("##SnapGridSize", &SnapGridSize, 1.0f, 1.0f, 128.0f, "%.0f");

        ImGui::Checkbox("Software Mouse Cursor", &io.MouseDrawCursor);  // FIXME-TESTS: Test engine always resets this value.

        bool content_stitching_available = _CaptureArgs.InCaptureWindows.Size <= 1;
#ifdef IMGUI_HAS_VIEWPORT
        content_stitching_available &= !(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable);
#endif
        ImGui::BeginDisabled(!content_stitching_available);
        ImGui::CheckboxFlags("Stitch full contents height", &_CaptureArgs.InFlags, ImGuiCaptureFlags_StitchAll);
        ImGui::EndDisabled();
        if (!content_stitching_available && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Content stitching is not possible when using viewports.");

        ImGui::CheckboxFlags("Include other windows", &_CaptureArgs.InFlags, ImGuiCaptureflags_IncludeOtherWindows);
        ImGui::CheckboxFlags("Include tooltips & popups", &_CaptureArgs.InFlags, ImGuiCaptureFlags_IncludeTooltipsAndPopups);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Capture area will be expanded to include visible tooltips.");

        ImGui::PopItemWidth();
        ImGui::TreePop();
    }

    ImGui::Separator();

    bool was_capturing = _StateIsCapturing;
    if (!_StateIsCapturing)
        _CaptureArgs.InCaptureWindows.clear();
    CaptureWindowPicker(&_CaptureArgs);
    CaptureWindowsSelector(&_CaptureArgs);

    if (!was_capturing && _StateIsCapturing)
    {
        // Create output folder and decide of output filename
        ImFormatString(_CaptureArgs.InOutputFile, IM_ARRAYSIZE(_CaptureArgs.InOutputFile), _OutputFileTemplate,
                       _FileCounter + 1);
        ImPathFixSeparatorsForCurrentOS(_CaptureArgs.InOutputFile);
        if (!ImFileCreateDirectoryChain(_CaptureArgs.InOutputFile, ImPathFindFilename(_CaptureArgs.InOutputFile)))
        {
            fprintf(stderr, "ImGuiCaptureContext: unable to create directory for file '%s'.\n",
                    _CaptureArgs.InOutputFile);
            _StateIsCapturing = false;
            if (Context.IsCapturingVideo())
                Context.EndVideoCapture();
        }

        // File template will most likely end with .png, but we need a different extension for videos.
        if (Context.IsCapturingVideo())
            if (char* ext = (char*)ImPathFindExtension(_CaptureArgs.InOutputFile))
                ImStrncpy(ext, VideoCaptureExt, (size_t)(ext - _CaptureArgs.InOutputFile));
    }

    ImGui::Separator();

    ImGui::End();
}

// Move/resize all windows so they are neatly aligned on a grid
// This is an easy way of ensuring some form of alignment without specifying detailed constraints.
void ImGuiCaptureToolUI::SnapWindowsToGrid(float cell_size)
{
    ImGuiContext& g = *GImGui;
    for (ImGuiWindow* window : g.Windows)
    {
        if (!window->WasActive)
            continue;

        if (window->Flags & ImGuiWindowFlags_ChildWindow)
            continue;

        if ((window->Flags & ImGuiWindowFlags_Popup) || (window->Flags & ImGuiWindowFlags_Tooltip))
            continue;

        ImRect rect = window->Rect();
        rect.Min.x = ImFloor(rect.Min.x / cell_size) * cell_size;
        rect.Min.y = ImFloor(rect.Min.y / cell_size) * cell_size;
        rect.Max.x = ImFloor(rect.Max.x / cell_size) * cell_size;
        rect.Max.y = ImFloor(rect.Max.y / cell_size) * cell_size;
        ImGui::SetWindowPos(window, rect.Min);
        ImGui::SetWindowSize(window, rect.GetSize());
    }
}

//-----------------------------------------------------------------------------
