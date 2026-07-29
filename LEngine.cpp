#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <deque>
#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <sstream>
#include <filesystem>
#include <cfloat>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_sdl3.h"
#include "imgui/backends/imgui_impl_sdlrenderer3.h"

using namespace std;
namespace fs = std::filesystem;

// --- State Machine ---
enum class GameState {
    MainMenu,
    Playing,
    Paused
};

// --- Save Data Structure ---
struct SaveData {
    float playerX;
    float playerY;
    int facing;
};

// --- Helper Functions for Saving / Loading ---
bool saveGame(const string& filename, float x, float y, int facing) {
    ofstream file(filename, ios::binary);
    if (!file.is_open()) return false;
    SaveData data{ x, y, facing };
    file.write(reinterpret_cast<const char*>(&data), sizeof(SaveData));
    return true;
}

bool loadGame(const string& filename, float& x, float& y, int& facing) {
    ifstream file(filename, ios::binary);
    if (!file.is_open()) return false;
    SaveData data;
    file.read(reinterpret_cast<char*>(&data), sizeof(SaveData));
    x = data.playerX;
    y = data.playerY;
    facing = data.facing;
    return true;
}

// --- Structures ---
struct SDLState {
    SDL_Window* window;
    SDL_Renderer* renderer;
};

struct TriggerZone {
    SDL_FRect bounds;
    string name;
    string text;
    bool triggered = false;
};

struct Camera2D {
    float x = 0.0f, y = 0.0f;
    float width = 640.0f, height = 320.0f;
};

struct MangaPanel {
    float x, y, w, h;
    string text;
    SDL_Texture* image = nullptr;
};

enum class Direction {
    Down = 0,
    Right = 1,
    Up = 2,
    Left = 3
};

// ==========================================
// --- IMGUI MUSIC PLAYER SUBSYSTEM ---
// ==========================================
struct MusicPlayerManager {
    MIX_Mixer* mixer = nullptr;
    MIX_Track* track = nullptr;
    MIX_Audio* currentAudio = nullptr;

    char directoryPath[1024] = "music";
    vector<fs::path> musicFiles;
    int selectedTrackIndex = -1;
    float masterVolume = 0.8f;
    bool isLooping = false;
    string nowPlayingTitle = "No Track Loaded";

    bool isSupportedAudioFile(const fs::path& filePath) {
        string ext = filePath.extension().string();
        transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return (ext == ".mp3" || ext == ".ogg" || ext == ".wav");
    }

    void scanDirectory(const char* path) {
        musicFiles.clear();
        selectedTrackIndex = -1;
        try {
            if (fs::exists(path) && fs::is_directory(path)) {
                for (const auto& entry : fs::directory_iterator(path)) {
                    if (entry.is_regular_file() && isSupportedAudioFile(entry.path())) {
                        musicFiles.push_back(entry.path());
                    }
                }
            }
        }
        catch (...) {
            // Handle filesystem permissions/errors gracefully
        }
    }

    void playTrack(int index) {
        if (index < 0 || index >= static_cast<int>(musicFiles.size())) return;

        if (track) {
            MIX_StopTrack(track, 0);
        }
        if (currentAudio) {
            MIX_DestroyAudio(currentAudio);
            currentAudio = nullptr;
        }

        fs::path selectedFile = musicFiles[index];
        currentAudio = MIX_LoadAudio(mixer, selectedFile.string().c_str(), false);

        if (currentAudio) {
            if (!track) {
                track = MIX_CreateTrack(mixer);
            }
            MIX_SetTrackAudio(track, currentAudio);

            SDL_PropertiesID props = SDL_CreateProperties();
            SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, isLooping ? -1 : 0);
            MIX_PlayTrack(track, props);
            SDL_DestroyProperties(props);

            selectedTrackIndex = index;
            nowPlayingTitle = selectedFile.filename().string();
        }
        else {
            nowPlayingTitle = "Failed to load: " + selectedFile.filename().string();
        }
    }

    bool init() {
        if (!MIX_Init()) return false;
        mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
        if (!mixer) return false;

        scanDirectory(directoryPath);
        return true;
    }

    void renderUI() {
        if (mixer) {
            MIX_SetMixerGain(mixer, masterVolume);
        }

        ImGui::InputText("Folder Path", directoryPath, IM_ARRAYSIZE(directoryPath));
        ImGui::SameLine();
        if (ImGui::Button("Scan Folder")) {
            scanDirectory(directoryPath);
        }

        ImGui::Separator();

        ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "Now Playing:");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", nowPlayingTitle.c_str());

        if (ImGui::Button("Play Selected")) {
            if (selectedTrackIndex >= 0) playTrack(selectedTrackIndex);
        }
        ImGui::SameLine();

        bool isPaused = track ? MIX_TrackPaused(track) : false;
        if (ImGui::Button(isPaused ? "Resume" : "Pause")) {
            if (track) {
                if (isPaused) MIX_ResumeTrack(track);
                else MIX_PauseTrack(track);
            }
        }
        ImGui::SameLine();

        if (ImGui::Button("Stop")) {
            if (track) MIX_StopTrack(track, 0);
        }
        ImGui::SameLine();

        if (ImGui::Checkbox("Loop Track", &isLooping)) {
            if (track) MIX_SetTrackLoops(track, isLooping ? -1 : 0);
        }

        ImGui::SliderFloat("Music Volume", &masterVolume, 0.0f, 1.0f, "%.2f");

        ImGui::Separator();
        ImGui::Text("Discovered Tracks (%zu found):", musicFiles.size());

        if (ImGui::BeginListBox("##TrackList", ImVec2(-FLT_MIN, 140))) {
            for (int i = 0; i < static_cast<int>(musicFiles.size()); ++i) {
                const bool isSelected = (selectedTrackIndex == i);
                string fileName = musicFiles[i].filename().string();

                if (ImGui::Selectable(fileName.c_str(), isSelected)) {
                    selectedTrackIndex = i;
                }

                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    playTrack(i);
                }

                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndListBox();
        }

        ImGui::TextDisabled("Tip: Double-click any file in the list to play it immediately.");
    }

    void destroy() {
        if (track) MIX_StopTrack(track, 0);
        if (currentAudio) MIX_DestroyAudio(currentAudio);
        if (mixer) MIX_DestroyMixer(mixer);
        MIX_Quit();
    }
};

// ==========================================
// --- SDL3 AUDIO MANAGEMENT SYSTEM ---
// ==========================================
struct SoundEffect {
    Uint8* buffer = nullptr;
    Uint32 length = 0;
    SDL_AudioSpec spec;
    SDL_AudioStream* stream = nullptr;
    bool loaded = false;

    bool load(const string& filepath) {
        destroy();

        if (!SDL_LoadWAV(filepath.c_str(), &spec, &buffer, &length)) {
            SDL_Log("Failed to load WAV (%s): %s", filepath.c_str(), SDL_GetError());
            return false;
        }

        stream = SDL_CreateAudioStream(&spec, &spec);
        if (!stream) {
            SDL_Log("Failed to create audio stream: %s", SDL_GetError());
            SDL_free(buffer);
            buffer = nullptr;
            return false;
        }

        if (!SDL_BindAudioStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, stream)) {
            SDL_Log("Failed to bind audio stream: %s", SDL_GetError());
            SDL_DestroyAudioStream(stream);
            SDL_free(buffer);
            stream = nullptr;
            buffer = nullptr;
            return false;
        }

        loaded = true;
        return true;
    }

    void play() {
        if (loaded && stream && buffer && length > 0) {
            SDL_ClearAudioStream(stream);
            SDL_PutAudioStreamData(stream, buffer, length);
            SDL_ResumeAudioStreamDevice(stream);
        }
    }

    void setVolume(float volume) {
        if (stream) {
            SDL_SetAudioStreamGain(stream, volume);
        }
    }

    void destroy() {
        if (stream) {
            SDL_UnbindAudioStream(stream);
            SDL_DestroyAudioStream(stream);
            stream = nullptr;
        }
        if (buffer) {
            SDL_free(buffer);
            buffer = nullptr;
        }
        loaded = false;
        length = 0;
    }
};

struct AudioManager {
    SoundEffect interactSfx;
    SoundEffect zoneSfx;
    SoundEffect saveSfx;
    float masterVolume = 0.8f;
    bool enabled = true;

    void init() {
        interactSfx.load("assets/audio/interact.wav");
        zoneSfx.load("assets/audio/zone.wav");
        saveSfx.load("assets/audio/save.wav");

        setMasterVolume(masterVolume);
    }

    void setMasterVolume(float vol) {
        masterVolume = clamp(vol, 0.0f, 1.0f);
        interactSfx.setVolume(masterVolume);
        zoneSfx.setVolume(masterVolume);
        saveSfx.setVolume(masterVolume);
    }

    void renderUI() {
        ImGui::Checkbox("SFX Audio Enabled", &enabled);
        if (ImGui::SliderFloat("SFX Master Volume", &masterVolume, 0.0f, 1.0f, "%.2f")) {
            setMasterVolume(masterVolume);
        }
        ImGui::Separator();
        ImGui::Text("Test Audio Triggers:");
        if (ImGui::Button("Play Interact SFX")) interactSfx.play();
        ImGui::SameLine();
        if (ImGui::Button("Play Zone SFX")) zoneSfx.play();
        ImGui::SameLine();
        if (ImGui::Button("Play Save SFX")) saveSfx.play();

        ImGui::BulletText("Interact SFX Status: %s", interactSfx.loaded ? "Loaded" : "Not Loaded (assets/audio/interact.wav)");
        ImGui::BulletText("Zone SFX Status: %s", zoneSfx.loaded ? "Loaded" : "Not Loaded (assets/audio/zone.wav)");
        ImGui::BulletText("Save SFX Status: %s", saveSfx.loaded ? "Loaded" : "Not Loaded (assets/audio/save.wav)");
    }

    void destroy() {
        interactSfx.destroy();
        zoneSfx.destroy();
        saveSfx.destroy();
    }
};

// ==========================================
// --- REUSABLE IMAGE INSPECTOR SYSTEM ---
// ==========================================
struct ImageInspectorState {
    char pathBuffer[256] = "";
    SDL_Texture* texture = nullptr;
    float width = 0.0f;
    float height = 0.0f;
    float zoom = 2.0f;
    bool isDragging = false;
    ImVec2 dragStart{ 0, 0 };
    float finalX = 0, finalY = 0, finalW = 0, finalH = 0;

    void load(SDL_Renderer* renderer, const char* path) {
        if (texture) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }
        snprintf(pathBuffer, sizeof(pathBuffer), "%s", path);
        texture = IMG_LoadTexture(renderer, pathBuffer);
        if (texture) {
            SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
            SDL_GetTextureSize(texture, &width, &height);
        }
        else {
            width = 0.0f;
            height = 0.0f;
        }
    }

    void destroy() {
        if (texture) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }
    }
};

void renderImageInspectorCanvas(const char* labelId, ImageInspectorState& state, SDL_Renderer* renderer, float canvasHeight = 220.0f) {
    ImGui::InputText("File Path", state.pathBuffer, sizeof(state.pathBuffer));
    ImGui::SameLine();
    if (ImGui::Button("Load Image")) {
        state.load(renderer, state.pathBuffer);
    }

    if (!state.texture) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No valid image loaded. Check the file path!");
        return;
    }

    ImGui::Text("Image Size: %.0fx%.0f px", state.width, state.height);
    ImGui::SliderFloat("Zoom", &state.zoom, 0.25f, 6.0f, "%.2fx");
    ImGui::Separator();

    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Selected Region Coordinates:");
    ImGui::Text("X: %.1f | Y: %.1f | W: %.1f | H: %.1f", state.finalX, state.finalY, state.finalW, state.finalH);

    string codeSnippet = "{ " + to_string((int)state.finalX) + ".0f, " +
        to_string((int)state.finalY) + ".0f, " +
        to_string((int)state.finalW) + ".0f, " +
        to_string((int)state.finalH) + ".0f },";

    char clipBuffer[128];
    snprintf(clipBuffer, sizeof(clipBuffer), "%s", codeSnippet.c_str());
    ImGui::InputText("Snippet", clipBuffer, sizeof(clipBuffer), ImGuiInputTextFlags_ReadOnly);

    if (ImGui::Button("Copy Snippet to Clipboard")) {
        ImGui::SetClipboardText(clipBuffer);
    }

    ImGui::Separator();
    ImGui::TextWrapped("Click & drag inside the canvas below to sample region coordinates.");

    float actualHeight = (canvasHeight > 0.0f) ? canvasHeight : ImGui::GetContentRegionAvail().y;

    string childWindowId = string("CanvasChild_") + labelId;
    ImGui::BeginChild(childWindowId.c_str(), ImVec2(0, actualHeight), true,
        ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove);

    ImVec2 canvasScreenPos = ImGui::GetCursorScreenPos();
    ImVec2 imageDisplaySize(state.width * state.zoom, state.height * state.zoom);

    ImGui::Image((ImTextureID)state.texture, imageDisplaySize);

    if (ImGui::IsItemHovered()) {
        ImVec2 mousePos = ImGui::GetIO().MousePos;
        float localX = (mousePos.x - canvasScreenPos.x) / state.zoom;
        float localY = (mousePos.y - canvasScreenPos.y) / state.zoom;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            state.isDragging = true;
            state.dragStart = ImVec2(localX, localY);
        }
    }

    if (state.isDragging) {
        ImVec2 mousePos = ImGui::GetIO().MousePos;
        float localX = (mousePos.x - canvasScreenPos.x) / state.zoom;
        float localY = (mousePos.y - canvasScreenPos.y) / state.zoom;

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            state.isDragging = false;
        }

        float x1 = min(state.dragStart.x, localX);
        float y1 = min(state.dragStart.y, localY);
        float x2 = max(state.dragStart.x, localX);
        float y2 = max(state.dragStart.y, localY);

        x1 = clamp(x1, 0.0f, state.width);
        x2 = clamp(x2, 0.0f, state.width);
        y1 = clamp(y1, 0.0f, state.height);
        y2 = clamp(y2, 0.0f, state.height);

        state.finalX = floor(x1);
        state.finalY = floor(y1);
        state.finalW = ceil(x2 - x1);
        state.finalH = ceil(y2 - y1);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 rectMin(canvasScreenPos.x + (state.finalX * state.zoom), canvasScreenPos.y + (state.finalY * state.zoom));
        ImVec2 rectMax(canvasScreenPos.x + ((state.finalX + state.finalW) * state.zoom), canvasScreenPos.y + ((state.finalY + state.finalH) * state.zoom));

        drawList->AddRect(rectMin, rectMax, IM_COL32(0, 255, 0, 255), 0.0f, 0, 2.0f);
        drawList->AddRectFilled(rectMin, rectMax, IM_COL32(0, 255, 0, 50));
    }

    ImGui::EndChild();
}

// ==========================================
// --- EVENT SYSTEM IMPLEMENTATION ---
// ==========================================
enum class EventType {
    ZoneEntered,
    ZoneExited,
    GameSaved,
    GameLoaded,
    DialogueTriggered,
    Interact,
    Custom
};

struct GameEvent {
    EventType type;
    float posX = 0.0f;
    float posY = 0.0f;
    string message;
    TriggerZone* zoneRef = nullptr;
};

using EventCallback = std::function<void(const GameEvent&)>;

class EventManager {
private:
    unordered_map<EventType, vector<EventCallback>> listeners;
    deque<GameEvent> eventQueue;

public:
    void subscribe(EventType type, EventCallback callback) {
        listeners[type].push_back(callback);
    }

    void publish(const GameEvent& event) {
        eventQueue.push_back(event);
    }

    void update() {
        while (!eventQueue.empty()) {
            GameEvent ev = eventQueue.front();
            eventQueue.pop_front();

            if (listeners.find(ev.type) != listeners.end()) {
                for (const auto& callback : listeners[ev.type]) {
                    callback(ev);
                }
            }
        }
    }
};

bool checkAABB(const SDL_FRect& a, const SDL_FRect& b) {
    return (a.x < b.x + b.w &&
        a.x + a.w > b.x &&
        a.y < b.y + b.h &&
        a.y + a.h > b.y);
}

// --- Animator ---
struct Animator {
    int currentFrame = 0;
    float frameTimer = 0.0f;
    float frameDuration = 0.12f;
    Direction facing = Direction::Down;
    bool isMoving = false;

    vector<vector<SDL_FRect>> animations;

    Animator() {
        animations.resize(4);

        animations[static_cast<int>(Direction::Down)] = {
            { 161.0f, 225.0f, 83.0f, 141.0f },
            { 252.0f, 226.0f, 85.0f, 135.0f },
            { 342.0f, 225.0f, 85.0f, 141.0f }
        };

        animations[static_cast<int>(Direction::Right)] = {
            { 476.0f, 227.0f, 86.0f, 137.0f },
            { 569.0f, 225.0f, 87.0f, 141.0f },
            { 659.0f, 224.0f, 88.0f, 140.0f }
        };

        animations[static_cast<int>(Direction::Up)] = {
            { 794.0f, 224.0f, 88.0f, 144.0f },
            { 896.0f, 224.0f, 80.0f, 146.0f },
            { 987.0f, 225.0f, 86.0f, 144.0f }
        };

        animations[static_cast<int>(Direction::Left)] = {
            { 1114.0f, 223.0f, 88.0f, 143.0f },
            { 1199.0f, 225.0f, 94.0f, 137.0f },
            { 1301.0f, 225.0f, 85.0f, 137.0f }
        };
    }

    void update(float deltaTime, float dx, float dy) {
        int dirIdx = static_cast<int>(facing);

        if (dx != 0.0f || dy != 0.0f) {
            isMoving = true;
            if (abs(dx) > abs(dy)) {
                facing = (dx > 0.0f) ? Direction::Right : Direction::Left;
            }
            else {
                facing = (dy > 0.0f) ? Direction::Down : Direction::Up;
            }

            dirIdx = static_cast<int>(facing);

            frameTimer += deltaTime;
            if (frameTimer >= frameDuration) {
                frameTimer = 0.0f;
                int totalFrames = static_cast<int>(animations[dirIdx].size());
                currentFrame = (currentFrame + 1) % totalFrames;
            }
        }
        else {
            isMoving = false;
            currentFrame = 0;
            frameTimer = 0.0f;
        }
    }

    SDL_FRect getSourceRect() const {
        int dirIdx = static_cast<int>(facing);
        if (animations.empty() || animations[dirIdx].empty()) {
            return { 0.0f, 0.0f, 32.0f, 32.0f };
        }
        return animations[dirIdx][currentFrame];
    }
};

struct ImageMap {
    int width = 0;
    int height = 0;
    vector<vector<bool>> collisionGrid;
    SDL_Texture* visualTexture = nullptr;

    bool loadMaps(SDL_Renderer* renderer, const string& collisionPath, const string& visualPath) {
        SDL_Surface* surface = IMG_Load(collisionPath.c_str());
        if (!surface) return false;
        SDL_Surface* converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surface);

        width = converted->w;
        height = converted->h;
        collisionGrid.assign(height, vector<bool>(width, false));

        int ignoreMinX = static_cast<int>(width * 0.15f), ignoreMaxX = static_cast<int>(width * 0.85f);
        int ignoreMinY = static_cast<int>(height * 0.35f), ignoreMaxY = static_cast<int>(height * 0.75f);

        Uint32* pixels = static_cast<Uint32*>(converted->pixels);
        const SDL_PixelFormatDetails* fmtDetails = SDL_GetPixelFormatDetails(converted->format);

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                Uint8 r, g, b, a;
                SDL_GetRGBA(pixels[y * (converted->pitch / 4) + x], fmtDetails, nullptr, &r, &g, &b, &a);
                if (r < 50 && g < 50 && b < 50) {
                    if (!(x >= ignoreMinX && x <= ignoreMaxX && y >= ignoreMinY && y <= ignoreMaxY))
                        collisionGrid[y][x] = true;
                }
            }
        }
        SDL_DestroySurface(converted);
        visualTexture = IMG_LoadTexture(renderer, visualPath.c_str());
        if (visualTexture) SDL_SetTextureScaleMode(visualTexture, SDL_SCALEMODE_NEAREST);
        return (visualTexture != nullptr);
    }

    bool isPixelSolid(int px, int py) const {
        if (px < 0 || px >= width || py < 0 || py >= height) return true;
        return collisionGrid[py][px];
    }
};

// --- Global UI State ---
deque<MangaPanel> panelQueue;
SDL_Texture* debugPanelImg = nullptr;
deque<string> eventSystemLog;

// --- Systems ---
void renderDialogueBox(const char* text, const SDL_Rect& gameViewport) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

    float boxX = static_cast<float>(gameViewport.x);
    float boxY = static_cast<float>(gameViewport.y + gameViewport.h + 10.0f);
    float boxWidth = static_cast<float>(gameViewport.w);
    float boxHeight = 90.0f;

    ImGui::SetNextWindowPos(ImVec2(boxX, boxY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(boxWidth, boxHeight), ImGuiCond_Always);

    ImGui::Begin("Dialogue", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

void renderMangaSystem() {
    if (panelQueue.empty()) return;

    MangaPanel& current = panelQueue.front();
    ImGui::SetNextWindowPos(ImVec2(current.x, current.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(current.w, current.h), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    ImGui::Begin("MangaPanel", nullptr, ImGuiWindowFlags_NoDecoration);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

    if (current.image) {
        ImGui::Image((ImTextureID)current.image, ImVec2(current.w - 15, (current.h / 2)));
        ImGui::Spacing();
    }

    ImGui::TextWrapped("%s", current.text.c_str());
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0.0f, ImGui::GetContentRegionAvail().y - 25.0f));
    if (ImGui::Button("Next Panel", ImVec2(-1, 0))) {
        panelQueue.pop_front();
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

void resolvePixelCollisions(SDL_FRect& entity, const ImageMap& map, bool checkX, float movementDelta) {
    int startX = (int)entity.x, endX = (int)(entity.x + entity.w);
    int startY = (int)entity.y, endY = (int)(entity.y + entity.h);
    for (int y = startY; y <= endY; ++y) {
        for (int x = startX; x <= endX; ++x) {
            if (map.isPixelSolid(x, y)) {
                if (checkX) entity.x -= (movementDelta > 0.0f) ? 1.0f : -1.0f;
                else entity.y -= (movementDelta > 0.0f) ? 1.0f : -1.0f;
                return;
            }
        }
    }
}

// --- Menu UI Systems ---
void renderMainMenuUI(GameState& currentState, SDL_FRect& dst, Animator& animator, const ImageMap& map, string& statusMessage, EventManager& eventManager) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGui::Begin("MainMenu", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings);

    float windowWidth = ImGui::GetWindowWidth();
    float windowHeight = ImGui::GetWindowHeight();

    ImGui::SetCursorPosY(windowHeight * 0.2f);
    ImGui::SetWindowFontScale(2.5f);
    float titleWidth = ImGui::CalcTextSize("LENGINE").x;
    ImGui::SetCursorPosX((windowWidth - titleWidth) * 0.5f);
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "LENGINE");
    ImGui::SetWindowFontScale(1.0f);

    ImGui::SetCursorPosY(windowHeight * 0.4f);
    ImVec2 btnSize(220, 45);
    float btnX = (windowWidth - btnSize.x) * 0.5f;

    ImGui::SetCursorPosX(btnX);
    if (ImGui::Button("New Game", btnSize)) {
        dst.x = (float)map.width / 2.0f;
        dst.y = (float)map.height / 2.0f;
        animator.facing = Direction::Down;
        currentState = GameState::Playing;
        statusMessage = "Started New Game.";
    }

    ImGui::Spacing();
    ImGui::SetCursorPosX(btnX);
    if (ImGui::Button("Load Game", btnSize)) {
        int facingInt = 0;
        if (loadGame("save.dat", dst.x, dst.y, facingInt)) {
            animator.facing = static_cast<Direction>(facingInt);
            currentState = GameState::Playing;
            statusMessage = "Game Loaded!";
            eventManager.publish({ EventType::GameLoaded, dst.x, dst.y, "Loaded game position." });
        }
        else {
            statusMessage = "No save file found (save.dat)!";
        }
    }

    ImGui::Spacing();
    ImGui::SetCursorPosX(btnX);
    if (ImGui::Button("Quit Game", btnSize)) {
        SDL_Event quitEv;
        quitEv.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quitEv);
    }

    if (!statusMessage.empty()) {
        ImGui::Spacing();
        float msgWidth = ImGui::CalcTextSize(statusMessage.c_str()).x;
        ImGui::SetCursorPosX((windowWidth - msgWidth) * 0.5f);
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", statusMessage.c_str());
    }

    ImGui::End();
}

void renderPauseMenuUI(GameState& currentState, SDL_FRect& dst, Animator& animator, string& statusMessage, EventManager& eventManager) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.35f, viewport->WorkPos.y + viewport->WorkSize.y * 0.25f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.3f, viewport->WorkSize.y * 0.5f), ImGuiCond_Always);

    ImGui::Begin("Pause Menu", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    float windowWidth = ImGui::GetWindowWidth();
    ImVec2 btnSize(windowWidth - 40.0f, 40.0f);

    ImGui::SetCursorPosX(20.0f);
    if (ImGui::Button("Resume Game", btnSize)) {
        currentState = GameState::Playing;
    }

    ImGui::Spacing();
    ImGui::SetCursorPosX(20.0f);
    if (ImGui::Button("Save Game", btnSize)) {
        if (saveGame("save.dat", dst.x, dst.y, static_cast<int>(animator.facing))) {
            statusMessage = "Game Saved Successfully!";
            eventManager.publish({ EventType::GameSaved, dst.x, dst.y, "Saved game state via Pause Menu." });
        }
        else {
            statusMessage = "Failed to save game!";
        }
    }

    ImGui::Spacing();
    ImGui::SetCursorPosX(20.0f);
    if (ImGui::Button("Return to Main Menu", btnSize)) {
        currentState = GameState::MainMenu;
    }

    if (!statusMessage.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", statusMessage.c_str());
    }

    ImGui::End();
}

// --- Main Application ---
int main(int argc, char* argv[]) {
    SDLState state;
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO);
    state.window = SDL_CreateWindow("LENGINE", 800, 600, SDL_WINDOW_RESIZABLE);
    state.renderer = SDL_CreateRenderer(state.window, nullptr);

    ImGui::CreateContext();
    ImGui_ImplSDL3_InitForSDLRenderer(state.window, state.renderer);
    ImGui_ImplSDLRenderer3_Init(state.renderer);

    // Initialize Audio Subsystem (SFX)
    AudioManager audioManager;
    audioManager.init();

    // Initialize Music Player Subsystem (Dear ImGui Music Player)
    MusicPlayerManager musicPlayer;
    musicPlayer.init();

    ImageMap map;
    map.loadMaps(state.renderer, "maps/map.png", "maps/mp_aest.png");

    GameState currentState = GameState::MainMenu;
    string menuStatusMessage = "";

    SDL_FRect dst{ (float)map.width / 2.0f, (float)map.height / 2.0f, 53.3f, 64.0f };

    SDL_Texture* spriteSheetTex = IMG_LoadTexture(state.renderer, "assets/kris.png");
    if (spriteSheetTex) {
        SDL_SetTextureScaleMode(spriteSheetTex, SDL_SCALEMODE_NEAREST);
    }

    // Load Railway Map texture
    SDL_Texture* railwayMapTex = IMG_LoadTexture(state.renderer, "assets/trainmap.png");
    if (railwayMapTex) {
        SDL_SetTextureScaleMode(railwayMapTex, SDL_SCALEMODE_NEAREST);
    }
    bool showRailwayMap = false;

    ImageInspectorState spriteInspector;
    spriteInspector.load(state.renderer, "assets/kris.png");

    ImageInspectorState mapInspector;
    mapInspector.load(state.renderer, "maps/mp_aest.png");
    mapInspector.zoom = 1.0f;

    static bool showDebugZones = false;
    static bool showMapInspectorWindow = true;
    static bool showAudioWindow = true;

    Animator animator;
    debugPanelImg = IMG_LoadTexture(state.renderer, "assets/manga_test.png");

    Camera2D camera{ 0, 0, 640, 320 };
    bool showDialogue = false;
    string dialogueText = "Press [E] to interact with nearby objects or zones!";

    EventManager eventManager;

    vector<TriggerZone> triggerZones = {
        { { (float)map.width / 2.0f - 100.0f, (float)map.height / 2.0f, 80.0f, 80.0f }, "West Shrine", "* You inspect the altar. An ancient power slumbers here..." },
        { { (float)map.width / 2.0f + 100.0f, (float)map.height / 2.0f, 80.0f, 80.0f }, "East Ruins", "* You examine the cracked pillars. Strange inscriptions cover the stone." }
    };

    eventManager.subscribe(EventType::ZoneEntered, [&](const GameEvent& ev) {
        stringstream ss;
        ss << "[EVENT] Entered Zone '" << ev.message << "'";
        eventSystemLog.push_front(ss.str());
        if (audioManager.enabled) audioManager.zoneSfx.play();
        });

    eventManager.subscribe(EventType::ZoneExited, [&](const GameEvent& ev) {
        stringstream ss;
        ss << "[EVENT] Exited Zone '" << ev.message << "'";
        eventSystemLog.push_front(ss.str());
        });

    eventManager.subscribe(EventType::Interact, [&](const GameEvent& ev) {
        stringstream ss;
        ss << "[EVENT] Pressed 'E' (Interact) at (" << (int)ev.posX << ", " << (int)ev.posY << ")";
        if (ev.zoneRef) ss << " on " << ev.zoneRef->name;
        eventSystemLog.push_front(ss.str());

        if (ev.zoneRef) {
            dialogueText = ev.zoneRef->text;
        }
        else {
            dialogueText = "* You searched the area, but found nothing of interest.";
        }
        showDialogue = true;
        if (audioManager.enabled) audioManager.interactSfx.play();
        });

    eventManager.subscribe(EventType::GameSaved, [&](const GameEvent& ev) {
        stringstream ss;
        ss << "[EVENT] Game Saved at (" << (int)ev.posX << ", " << (int)ev.posY << ")";
        eventSystemLog.push_front(ss.str());
        if (audioManager.enabled) audioManager.saveSfx.play();
        });

    eventManager.subscribe(EventType::GameLoaded, [&](const GameEvent& ev) {
        stringstream ss;
        ss << "[EVENT] Game Loaded at (" << (int)ev.posX << ", " << (int)ev.posY << ")";
        eventSystemLog.push_front(ss.str());
        if (audioManager.enabled) audioManager.saveSfx.play();
        });

    eventManager.subscribe(EventType::DialogueTriggered, [&](const GameEvent& ev) {
        stringstream ss;
        ss << "[EVENT] Dialogue Triggered: " << ev.message;
        eventSystemLog.push_front(ss.str());
        });

    Uint64 lastTime = SDL_GetTicks();
    bool running = true;

    while (running) {
        Uint64 currentTime = SDL_GetTicks();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) running = false;

            if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_F) {
                    bool isFullscreen = (SDL_GetWindowFlags(state.window) & SDL_WINDOW_FULLSCREEN);
                    SDL_SetWindowFullscreen(state.window, isFullscreen ? 0 : SDL_WINDOW_FULLSCREEN);
                }

                if (event.key.key == SDLK_F2) {
                    showDebugZones = !showDebugZones;
                }

                if (event.key.key == SDLK_M && currentState == GameState::Playing) {
                    showRailwayMap = !showRailwayMap;
                }

                if (event.key.key == SDLK_ESCAPE) {
                    if (currentState == GameState::Playing) currentState = GameState::Paused;
                    else if (currentState == GameState::Paused) currentState = GameState::Playing;
                }

                if (event.key.key == SDLK_E && currentState == GameState::Playing) {
                    bool foundZone = false;
                    for (auto& zone : triggerZones) {
                        if (checkAABB(dst, zone.bounds)) {
                            eventManager.publish({ EventType::Interact, dst.x, dst.y, zone.name, &zone });
                            foundZone = true;
                            break;
                        }
                    }
                    if (!foundZone) {
                        eventManager.publish({ EventType::Interact, dst.x, dst.y, "General Interaction" });
                    }
                }
            }
        }

        if (currentState == GameState::Playing) {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            float dx = (float)(keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]);
            float dy = (float)(keys[SDL_SCANCODE_S] - keys[SDL_SCANCODE_W]);

            if (panelQueue.empty() && !showRailwayMap) {
                if (dx != 0.0f) { dst.x += dx * 180.0f * deltaTime; resolvePixelCollisions(dst, map, true, dx); }
                if (dy != 0.0f) { dst.y += dy * 180.0f * deltaTime; resolvePixelCollisions(dst, map, false, dy); }

                animator.update(deltaTime, dx, dy);
            }
            else {
                animator.update(deltaTime, 0.0f, 0.0f);
            }

            for (auto& zone : triggerZones) {
                bool overlaps = checkAABB(dst, zone.bounds);
                if (overlaps && !zone.triggered) {
                    zone.triggered = true;
                    eventManager.publish({ EventType::ZoneEntered, dst.x, dst.y, zone.name, &zone });
                }
                else if (!overlaps && zone.triggered) {
                    zone.triggered = false;
                    eventManager.publish({ EventType::ZoneExited, dst.x, dst.y, zone.name, &zone });
                }
            }

            camera.x = (dst.x + dst.w / 2.0f) - (camera.width / 2.0f);
            camera.y = (dst.y + dst.h / 2.0f) - (camera.height / 2.0f);
        }

        eventManager.update();

        SDL_SetRenderLogicalPresentation(state.renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
        SDL_SetRenderDrawColor(state.renderer, 0, 0, 0, 255);
        SDL_RenderClear(state.renderer);

        int winW, winH;
        SDL_GetCurrentRenderOutputSize(state.renderer, &winW, &winH);
        SDL_Rect gameViewport = {
            (winW - 640) / 2,
            (winH - 320) / 2,
            640,
            320
        };

        if (currentState == GameState::Playing || currentState == GameState::Paused) {
            SDL_SetRenderViewport(state.renderer, &gameViewport);
            SDL_Rect clipRect = { 0, 0, 640, 320 };
            SDL_SetRenderClipRect(state.renderer, &clipRect);

            SDL_FRect visualRect = { -camera.x, -camera.y, (float)map.width, (float)map.height };
            SDL_FRect playerRect = { dst.x - camera.x, dst.y - camera.y, dst.w, dst.h };
            SDL_FRect srcRect = animator.getSourceRect();

            if (map.visualTexture) SDL_RenderTexture(state.renderer, map.visualTexture, nullptr, &visualRect);

            if (showDebugZones) {
                SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_BLEND);
                for (const auto& zone : triggerZones) {
                    SDL_FRect zoneWorldRect = { zone.bounds.x - camera.x, zone.bounds.y - camera.y, zone.bounds.w, zone.bounds.h };
                    if (zone.triggered) SDL_SetRenderDrawColor(state.renderer, 255, 250, 0, 100);
                    else SDL_SetRenderDrawColor(state.renderer, 0, 200, 255, 75);
                    SDL_RenderFillRect(state.renderer, &zoneWorldRect);
                }
            }

            if (spriteSheetTex) SDL_RenderTexture(state.renderer, spriteSheetTex, &srcRect, &playerRect);
        }

        SDL_SetRenderViewport(state.renderer, nullptr);
        SDL_SetRenderClipRect(state.renderer, nullptr);

        ImGui_ImplSDLRenderer3_NewFrame(); ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();

        if (currentState == GameState::MainMenu) {
            renderMainMenuUI(currentState, dst, animator, map, menuStatusMessage, eventManager);
        }
        else if (currentState == GameState::Playing || currentState == GameState::Paused) {
            ImGui::Begin("Debug Control Panel");

            if (ImGui::CollapsingHeader("Trigger Zones Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("Show Zone Overlays (Shortcut: F2)", &showDebugZones);

                if (ImGui::Button("Hide All Zones")) showDebugZones = false;
                ImGui::SameLine();
                if (ImGui::Button("Show All Zones")) showDebugZones = true;

                ImGui::Separator();
                ImGui::Text("Active Trigger Zones Count: %zu", triggerZones.size());
                for (const auto& zone : triggerZones) {
                    ImGui::BulletText("%s [%s]", zone.name.c_str(), zone.triggered ? "PLAYER INSIDE" : "Idle");
                }
            }

            if (ImGui::CollapsingHeader("Event System Console", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Button("Dispatch Custom Event")) {
                    eventManager.publish({ EventType::DialogueTriggered, dst.x, dst.y, "Manual event emitted via debug menu!" });
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Log")) {
                    eventSystemLog.clear();
                }

                ImGui::BeginChild("EventLogChild", ImVec2(0, 100), true);
                for (const auto& log : eventSystemLog) {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", log.c_str());
                }
                ImGui::EndChild();
            }

            if (ImGui::CollapsingHeader("Game State & Saving", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Button("Save Game Now")) {
                    if (saveGame("save.dat", dst.x, dst.y, static_cast<int>(animator.facing))) {
                        menuStatusMessage = "Quick Saved successfully!";
                        eventManager.publish({ EventType::GameSaved, dst.x, dst.y, "Quick save executed." });
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Load Game Now")) {
                    int facingInt = 0;
                    if (loadGame("save.dat", dst.x, dst.y, facingInt)) {
                        animator.facing = static_cast<Direction>(facingInt);
                        menuStatusMessage = "Quick Loaded successfully!";
                        eventManager.publish({ EventType::GameLoaded, dst.x, dst.y, "Quick load executed." });
                    }
                }
                ImGui::Text("%s", menuStatusMessage.c_str());
            }

            if (ImGui::CollapsingHeader("Display Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("Show Static Dialogue", &showDialogue);
                ImGui::Checkbox("Show Railway Map (M)", &showRailwayMap);
                ImGui::Checkbox("Show Map Inspector Window", &showMapInspectorWindow);
                ImGui::Checkbox("Show Audio System Window", &showAudioWindow);
                ImGui::InputText("Static Text", &dialogueText[0], 128);
                if (ImGui::Button("Toggle Fullscreen (F)")) {
                    bool isFullscreen = (SDL_GetWindowFlags(state.window) & SDL_WINDOW_FULLSCREEN);
                    SDL_SetWindowFullscreen(state.window, isFullscreen ? 0 : SDL_WINDOW_FULLSCREEN);
                }
            }

            if (ImGui::CollapsingHeader("Manga System", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text("Active Panels in Queue: %zu", panelQueue.size());
                ImGui::Separator();

                if (ImGui::Button("Trigger Scene: Text Only")) {
                    panelQueue.push_back({ 100, 100, 300, 200, "*RUMBLE* The ground began to shake..." });
                    panelQueue.push_back({ 400, 50, 350, 250, "What was that?! I need to get out of here!" });
                }

                if (ImGui::Button("Trigger Scene: With Image")) {
                    panelQueue.push_back({ 250, 50, 300, 400, "A massive shadow loomed ahead...", debugPanelImg });
                }

                if (ImGui::Button("Clear All Panels", ImVec2(-1, 0))) {
                    panelQueue.clear();
                }
            }

            if (ImGui::CollapsingHeader("Sprite Sheet Inspector", ImGuiTreeNodeFlags_DefaultOpen)) {
                renderImageInspectorCanvas("SpriteInspector", spriteInspector, state.renderer, 220.0f);
            }

            ImGui::End();

            if (showAudioWindow) {
                ImGui::SetNextWindowSize(ImVec2(450, 550), ImGuiCond_FirstUseEver);
                ImGui::Begin("Audio System Debug", &showAudioWindow);

                if (ImGui::CollapsingHeader("Music Player", ImGuiTreeNodeFlags_DefaultOpen)) {
                    musicPlayer.renderUI();
                }

                if (ImGui::CollapsingHeader("Sound Effects (SFX)", ImGuiTreeNodeFlags_DefaultOpen)) {
                    audioManager.renderUI();
                }

                ImGui::End();
            }

            if (showMapInspectorWindow) {
                ImGui::SetNextWindowSize(ImVec2(600, 650), ImGuiCond_FirstUseEver);
                ImGui::Begin("Map Inspector", &showMapInspectorWindow);
                renderImageInspectorCanvas("MapInspector", mapInspector, state.renderer, 450.0f);
                ImGui::End();
            }

            // Render Railway Map Popup/Overlay via Dear ImGui when 'M' is pressed
            if (showRailwayMap && railwayMapTex) {
                ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
                ImGui::Begin("Underground Railway Map", &showRailwayMap, ImGuiWindowFlags_NoCollapse);
                ImGui::Text("Current Subway / Underground Network Map:");
                ImGui::Separator();
                ImVec2 availSize = ImGui::GetContentRegionAvail();
                ImGui::Image((ImTextureID)railwayMapTex, availSize);
                ImGui::End();
            }

            if (showDialogue) renderDialogueBox(dialogueText.c_str(), gameViewport);
            renderMangaSystem();

            if (currentState == GameState::Paused) {
                renderPauseMenuUI(currentState, dst, animator, menuStatusMessage, eventManager);
            }
        }

        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), state.renderer);
        SDL_RenderPresent(state.renderer);
    }

    // --- CLEANUP ---
    musicPlayer.destroy();
    audioManager.destroy();
    spriteInspector.destroy();
    mapInspector.destroy();

    if (spriteSheetTex) SDL_DestroyTexture(spriteSheetTex);
    if (railwayMapTex) SDL_DestroyTexture(railwayMapTex);
    if (debugPanelImg) SDL_DestroyTexture(debugPanelImg);
    if (map.visualTexture) SDL_DestroyTexture(map.visualTexture);

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (state.renderer) SDL_DestroyRenderer(state.renderer);
    if (state.window) SDL_DestroyWindow(state.window);
    SDL_Quit();
    return 0;
}