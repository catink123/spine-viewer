#include <spine/spine-sfml.h>

#include <SFML/Window.hpp>
#include <SFML/Graphics.hpp>

#include <imgui.h>
#include <imgui-SFML.h>

#include <filesystem>
#include "font.c"
namespace fs = std::filesystem;

const unsigned int RECORDING_PATH_SIZE = 1024;

struct SVState {
	sf::Vector2f position;
	float zoom = 1;
	bool loop = true;
	bool usePremultiplyAlpha = true;
	spine::String currentAnimation;
	spine::String currentSkin;
	float time = 0;

	bool isRecording = false;
	float recordingFPS = 30.f;
	char recordingPath[RECORDING_PATH_SIZE];

	sf::Vector2f textureSize;
	std::vector<spine::String> animations;
	std::vector<spine::String> skins;

	bool _isMousePressing = false;
	sf::Vector2i _prevMousePos;

	sf::RenderTexture skeletonTexture;

	SVState() {
		memset(recordingPath, 0, RECORDING_PATH_SIZE);
	}
};

struct SkeletonBundle {
	spine::Atlas* atlas;
	spine::SkeletonData* data;

	SkeletonBundle(const char* atlasPath, const char* dataPath, bool isDataJson = false)
	{
		if (!fs::is_regular_file(atlasPath) || !fs::is_regular_file(dataPath)) {
			throw std::runtime_error("invalid atlas/data file path");
		}

		atlas = new spine::Atlas(atlasPath, &textureLoader);
		if (isDataJson) {
			spine::SkeletonJson skeletonJson(atlas);
			data = skeletonJson.readSkeletonDataFile(dataPath);
		}
		else {
			spine::SkeletonBinary skeletonBinary(atlas);
			data = skeletonBinary.readSkeletonDataFile(dataPath);
		}

		if (!data) {
			throw std::runtime_error("invalid data file");
		}
	}

	~SkeletonBundle() {
		delete atlas;
	}

private:
	spine::SFMLTextureLoader textureLoader;
};

// Note: resets the Skeleton's position to (0, 0) and
// overrides the 0th track's animation
sf::FloatRect getMaxSkeletonBounds(
	spine::SkeletonDrawable& skeletonDrawable, 
	spine::Animation* anim,
	float precision = 0.016
) {
	spine::Skeleton* skeleton = skeletonDrawable.skeleton;
	spine::AnimationState* animState = skeletonDrawable.state;
	skeleton->setPosition(0, 0);
	animState->setAnimation(0, anim, false);
	skeletonDrawable.update(0);

	// saved (accumulated) bounds XYWH
	float sbX = 0, sbY = 0, sbW = 0, sbH = 0;
	spine::Vector<float> tempVec;

	spine::TrackEntry* track = animState->getTracks()[0];
	float animEnd = track->getAnimationEnd();
	for (float i = 0; i <= animEnd; i += precision) {
		// current bounds XYWH
		float bX = 0, bY = 0, bW = 0, bH = 0;

		skeleton->getBounds(bX, bY, bW, bH, tempVec);

		// Set up initial maxBounds on the first iteration...
		if (i == 0) {
			sbX = bX;
			sbY = bY;
			sbW = bW;
			sbH = bH;
		}
		// ... and expand outward from it
		else {
			float minX = fminf(sbX, bX);
			sbW += sbX - minX;
			sbX = minX;

			float minY = fminf(sbY, bY);
			sbH += sbY - minY;
			sbY = minY;

			sbW = fmaxf(sbW, bW + bX - sbX);
			sbH = fmaxf(sbH, bH + bY - sbY);
		}

		skeletonDrawable.update(precision);
		tempVec.clear();
	}

	// incorrect use of FloatRect's top parameter,
	// but Skeleton.setPosition's Y axis goes bottom to top (OpenGL)
	// instead of top to bottom (SFML), so this is a weird fix of that
	return sf::FloatRect(sbX, sbY, sbW, sbH);
}

class SkeletonRenderer {
	SkeletonBundle bundle;
	spine::SkeletonDrawable drawable;
	sf::RenderTexture renderTexture;
	sf::Sprite sprite;
	sf::RectangleShape border;

	sf::FloatRect maxBounds;
	sf::Vector2f skeletonPosition;

	bool isRecording = false;
	float recordingFrameTime = 1.f / 30.f;
	unsigned int currentRecordingFrame = 0;

	void renderInternalTexture() {
		renderTexture.clear(sf::Color::Transparent);
		renderTexture.draw(drawable);
		renderTexture.display();
	}

public:
	bool drawBorder = false;
	std::string recordingPath = ".";
	float renderScale = 1.f;

	SkeletonRenderer(
		const char* atlasPath, 
		const char* dataPath, 
		bool isDataJson = false
	) : bundle(atlasPath, dataPath, isDataJson),
		drawable(bundle.data)
	{
		drawable.timeScale = 1;
		spine::Skeleton* skeleton = drawable.skeleton;
		if (!skeleton) {
			throw std::runtime_error("invalid skeleton data");
		}

		// Set the first animation on a loop as default
		setAnimation(getAnimations()[0]->getName(), false);
		setLoop(true);

		// Set up the skeleton
		skeleton->setToSetupPose();
		skeleton->updateWorldTransform();

		// Set the first skin as default
		auto skins = getSkins();
		if (skins.size() > 0) {
			setSkin(skins[0]->getName(), false);
		}

		// Create the initial RenderTexture for the sprite
		updateRenderTexture();
	}

	spine::Vector<spine::Animation*>& getAnimations() {
		return bundle.data->getAnimations();
	}
	spine::Vector<spine::Skin*>& getSkins() {
		return bundle.data->getSkins();
	}

	spine::Vector<spine::TrackEntry*>& getTracks() {
		return drawable.state->getTracks();
	}

	spine::Animation* getCurrentAnimation() {
		auto tracks = getTracks();
		if (tracks.size() == 0) {
			return nullptr;
		}
		return tracks[0]->getAnimation();
	}
	spine::Skin* getCurrentSkin() {
		return drawable.skeleton->getSkin();
	}

	bool setAnimation(spine::String name, bool doUpdateRT = true) {
		auto tracks = getTracks();
		bool prevLoop = true;

		if (tracks.size() > 0) {
			prevLoop = tracks[0]->getLoop();

			if (tracks[0]->getAnimation()->getName() == name) {
				return false;
			}
		}

		// Sanity check for if the given name is of a nonexisting Animation
		auto& anims = getAnimations();
		spine::Animation* found = nullptr;
		for (int i = 0; i < anims.size(); i++) {
			if (anims[i]->getName() == name) {
				found = anims[i];
				break;
			}
		}
		if (!found) {
			return false;
		}

		drawable.state->setAnimation(0, found, prevLoop);
		if (doUpdateRT) {
			updateRenderTexture();
		}
		return true;
	}
	void setSkin(spine::String name, bool doUpdateRT = true) {
		auto currentSkin = drawable.skeleton->getSkin();
		if (currentSkin && currentSkin->getName() == name) {
			return;
		}

		drawable.skeleton->setSkin(name);
		drawable.skeleton->setSlotsToSetupPose();
		if (doUpdateRT) {
			updateRenderTexture();
		}
	}

	void setLoop(bool state) {
		auto tracks = getTracks();
		if (tracks.size() == 0) {
			return;
		}

		auto track = tracks[0];
		if (track->getLoop() == state) {
			return;
		}

		track->setLoop(state);
	}

	void setTimeScale(float timeScale) {
		drawable.timeScale = timeScale;
	}
	const float& getTimeScale() {
		return getTimeScaleRef();
	}

	float& getTimeScaleRef() {
		return drawable.timeScale;
	}

	void setPremultiplyAlpha(bool value) {
		drawable.setUsePremultipliedAlpha(value);
	}
	
	bool getPremultiplyAlpha() {
		return drawable.getUsePremultipliedAlpha();
	}

	void updateRenderTexture() {
		maxBounds = getMaxSkeletonBounds(drawable, getCurrentAnimation());
		bool creationResult = renderTexture.create((unsigned int)maxBounds.width, (unsigned int)maxBounds.height);
		if (!creationResult) {
			throw std::runtime_error("couldn't create the Skeleton's RenderTexture");
		}

		sprite = sf::Sprite(renderTexture.getTexture());
		border = sf::RectangleShape(sf::Vector2f(maxBounds.width, maxBounds.height));
		border.setOutlineColor(sf::Color::Red);
		border.setOutlineThickness(2.f);
		border.setFillColor(sf::Color::Transparent);

		setSkeletonPosition(-maxBounds.getPosition());
	}

	float getScale() {
		return sprite.getScale().x;
	}

	sf::Vector2f getTextureSize() {
		return maxBounds.getSize();
	}

	void setScale(float amount) {
		if (sprite.getScale().x == amount) {
			return;
		}
		sprite.setScale(amount, amount);
		border.setScale(amount, amount);
		border.setOutlineThickness(2.f / amount);
	}

	float getAnimCurrentTime() {
		auto tracks = getTracks();
		if (tracks.size() == 0) {
			return 0;
		}

		return tracks[0]->getAnimationTime();
	}

	void setAnimCurrentTime(float time) {
		auto tracks = getTracks();
		if (tracks.size() == 0) {
			return;
		}

		if (tracks[0]->getTrackTime() == time) {
			return;
		}

		tracks[0]->setTrackTime(time);
	}

	float getAnimDuration() {
		auto tracks = getTracks();
		if (tracks.size() == 0) {
			return 0;
		}

		return tracks[0]->getAnimationEnd();
	}

	void setSkeletonPosition(sf::Vector2f pos) {
		skeletonPosition = pos;
		drawable.skeleton->setPosition(pos.x, pos.y);
	}

	sf::Vector2f getSkeletonPosition() {
		return skeletonPosition;
	}
	
	void update(float delta) {
		if (!isRecording) {
			drawable.update(delta);
			return;
		}

		fs::path filename = recordingPath;
		filename /= getCurrentAnimation()->getName().buffer();
		filename += "_" + std::to_string(currentRecordingFrame) + ".png";
		auto renderedImage = renderTexture.getTexture().copyToImage();
		renderedImage.saveToFile(filename.string());

		currentRecordingFrame++;
		drawable.update(recordingFrameTime);
		float currentTime = getTracks()[0]->getTrackTime();
		if (currentTime >= getAnimDuration()) {
			isRecording = false;
			// Restore the initial texture scale after recording is done
			if (renderScale != 1) {
				drawable.skeleton->setScaleX(1.f);
				drawable.skeleton->setScaleY(1.f);
				updateRenderTexture();
			}
		}
	}

	void setRecordingFPS(float fps) {
		if (!isRecording) {
			recordingFrameTime = 1.f / fps;
		}
	}

	float getRecordingFPS() {
		return 1.f / recordingFrameTime;
	}

	// Returns true if setting the state was successful
	bool setIsRecording(bool state) {
		if (isRecording == state) {
			return true;
		}

		if (state && !fs::is_directory(recordingPath)) {
			printf("The supplied recording path '%s' is not a directory! Check the path for errors and make sure the directory exists.\n", recordingPath.c_str());
			return false;
		}

		isRecording = state;

		if (state) {
			currentRecordingFrame = 0;
			if (renderScale != 1) {
				drawable.skeleton->setScaleX(renderScale);
				drawable.skeleton->setScaleY(renderScale);
				updateRenderTexture();
			}
			setAnimCurrentTime(0.f);
			setTimeScale(1.f);
			drawable.update(0.f);
			setLoop(false);
			renderInternalTexture();
		}

		return true;
	}

	bool getIsRecording() {
		return isRecording;
	}

	void render(sf::RenderWindow& window) {
		renderInternalTexture();

		window.draw(sprite);
		if (drawBorder) {
			window.draw(border);
		}
	}
};

void toolsWindow(SVState& state, SkeletonRenderer* skeleton) {
	ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(5, 5), ImGuiCond_FirstUseEver);

	ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::Checkbox("Display Border", &skeleton->drawBorder);
	ImGui::Checkbox("Premultiply Alpha", &state.usePremultiplyAlpha);
	ImGui::SliderFloat("Position X", &state.position.x, state.textureSize.x * -0.5f, state.textureSize.x * 1.5f);
	ImGui::SliderFloat("Position Y", &state.position.y, state.textureSize.y * -0.5f, state.textureSize.y * 1.5f);
	ImGui::DragFloat("Zoom", &state.zoom, 0.05f, 0.00f, 3.f, "%.2f");

	if (ImGui::BeginCombo("Animation", state.currentAnimation.buffer())) {
		for (const auto& anim : state.animations) {
			if (ImGui::Selectable(anim.buffer())) {
				state.currentAnimation = anim;
			}
		}
		ImGui::EndCombo();
	}

	if (ImGui::BeginCombo("Skin", state.currentSkin.buffer())) {
		for (const auto& skin : state.skins) {
			if (ImGui::Selectable(skin.buffer())) {
				state.currentSkin = skin;
			}
		}
		ImGui::EndCombo();
	}

	ImGui::End();
}

void controlWindow(SVState& state, SkeletonRenderer* skeleton) {
	ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(697, 5), ImGuiCond_FirstUseEver);

	ImGui::Begin("Animation Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

	ImGui::SliderFloat("Time", &state.time, 0, skeleton->getAnimDuration(), "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::DragFloat("TimeScale", &skeleton->getTimeScaleRef(), 0.05f, 0.f, 3.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::Checkbox("Loop", &state.loop);

	ImGui::End();
}

void recordingWindow(SVState& state, SkeletonRenderer* skeleton) {
	ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(344, 5), ImGuiCond_FirstUseEver);

	ImGui::Begin("Recording Panel", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

	ImVec2 maxWidth(ImGui::GetContentRegionAvail().x, 0.f);

	if (!state.isRecording) {
		if (ImGui::Button("Record", maxWidth)) {
			state.isRecording = true;
		}
		ImGui::DragFloat("FPS", &state.recordingFPS, 1.0f, 0.01f, 60.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::DragFloat("Render Scale", &skeleton->renderScale, 0.05f, 0.01f, 3.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::InputText("Save Path", state.recordingPath, RECORDING_PATH_SIZE);
	}
	else {
		if (ImGui::Button("Stop Recording", maxWidth)) {
			state.isRecording = false;
		}
		float recordingProgress = skeleton->getAnimCurrentTime() / skeleton->getAnimDuration();
		ImGui::ProgressBar(recordingProgress);
	}

	ImGui::End();
}

ImFont* font = nullptr;

void setupStyle() {
	auto& style = ImGui::GetStyle();
	style.WindowRounding = 6.f;
	style.FrameRounding = 3.f;
	style.AntiAliasedLines = true;

	ImGuiIO& igio = ImGui::GetIO();
	font = igio.Fonts->AddFontFromMemoryCompressedTTF(NotoSans_compressed_data, NotoSans_compressed_size, 16);
	ImGui::SFML::UpdateFontTexture();
}

void printUsage(const char* binaryName) {
	printf("Usage: %s <atlas-path> [--json|-j] <skel-path>\n", binaryName);
}

int main(int argc, char* argv[]) {
	if (argc < 3) {
		printUsage(argv[0]);
		return 1;
	}

	bool usingJson = false;

	if (argc == 4) {
		if (strcmp(argv[2], "--json") != 0 || strcmp(argv[2], "-j") != 0) {
			printUsage(argv[0]);
			return 1;
		}
		usingJson = true;
	}

	SkeletonRenderer* skeleton = nullptr;

	try {
		skeleton = new SkeletonRenderer(argv[1], argv[2], usingJson);
	}
	catch (std::runtime_error& e) {
		printf("Couldn't load the skeleton: %s", e.what());
		return 1;
	}

	SVState state;
	state.position = skeleton->getSkeletonPosition();

	sf::RenderWindow window(sf::VideoMode(1280, 720), "Spine Viewer");
	sf::Clock deltaClock;

	ImGui::SFML::Init(window);
	ImGuiIO& igio = ImGui::GetIO();
	setupStyle();

	auto& anims = skeleton->getAnimations();
	for (int i = 0; i < anims.size(); i++) {
		state.animations.push_back(anims[i]->getName());
	}
	state.currentAnimation = skeleton->getCurrentAnimation()->getName();

	auto& skins = skeleton->getSkins();
	for (int i = 0; i < skins.size(); i++) {
		state.skins.push_back(skins[i]->getName());
	}
	state.currentSkin = skeleton->getCurrentSkin()->getName();

	while (window.isOpen()) {
		sf::Event ev;
		while (window.pollEvent(ev)) {
			ImGui::SFML::ProcessEvent(window, ev);

			if (ev.type == sf::Event::Closed) {
				window.close();
			}

			if (ev.type == sf::Event::Resized) {
				auto& sizeEvent = ev.size;

				const sf::View& prevView = window.getView();

				sf::FloatRect newRect(prevView.getCenter() - prevView.getSize() / 2.0f, sf::Vector2f(window.getSize()));

				window.setView(sf::View(newRect));
			}

			if (igio.WantCaptureMouse) {
				continue;
			}

			if (ev.type == sf::Event::MouseMoved && state._isMousePressing) {
				auto& mouseMoveEvent = ev.mouseMove;
				sf::Vector2i currentMousePos(mouseMoveEvent.x, mouseMoveEvent.y);

				sf::Vector2i deltaMousePos = currentMousePos - state._prevMousePos;
				sf::View currentView = window.getView();
				currentView.move(-sf::Vector2f(deltaMousePos));
				window.setView(currentView);

				state._prevMousePos = currentMousePos;
			}

			if (ev.type == sf::Event::MouseButtonPressed) {
				auto& mousePressedEvent = ev.mouseButton;
				if (mousePressedEvent.button == sf::Mouse::Left) {
					state._prevMousePos.x = mousePressedEvent.x;
					state._prevMousePos.y = mousePressedEvent.y;
					state._isMousePressing = true;
				}
			}

			if (ev.type == sf::Event::MouseButtonReleased) {
				auto& mousePressedEvent = ev.mouseButton;
				if (mousePressedEvent.button == sf::Mouse::Left) {
					state._isMousePressing = false;
				}
			}

			if (ev.type == sf::Event::MouseWheelScrolled) {
				auto& mouseWheelEvent = ev.mouseWheelScroll;
				state.zoom += (float)mouseWheelEvent.delta * 0.02f;
			}
		}

		sf::Time delta = deltaClock.getElapsedTime();
		ImGui::SFML::Update(window, delta);
		ImGui::PushFont(font);

		float deltaF = delta.asSeconds();
		deltaClock.restart();

		state.textureSize = skeleton->getTextureSize();

		state.position = skeleton->getSkeletonPosition();
		if (!state.isRecording) {
			toolsWindow(state, skeleton);
		}
		skeleton->setPremultiplyAlpha(state.usePremultiplyAlpha);
		skeleton->setScale(state.zoom);
		if (skeleton->setAnimation(state.currentAnimation)) {
			state.position = skeleton->getSkeletonPosition();
		}
		skeleton->setSkin(state.currentSkin);
		skeleton->setSkeletonPosition(state.position);

		if (!state.isRecording) {
			controlWindow(state, skeleton);
		}
		skeleton->setAnimCurrentTime(state.time);
		skeleton->setLoop(state.loop);

		state.isRecording = skeleton->getIsRecording();
		state.recordingFPS = skeleton->getRecordingFPS();
		memcpy(state.recordingPath, skeleton->recordingPath.c_str(), RECORDING_PATH_SIZE);
		recordingWindow(state, skeleton);
		// If the setting was unsuccessful, return the previous isRecording value
		if (!skeleton->setIsRecording(state.isRecording)) {
			state.isRecording = false;
		}
		skeleton->setRecordingFPS(state.recordingFPS);
		skeleton->recordingPath = state.recordingPath;

		skeleton->update(deltaF);
		state.time = skeleton->getAnimCurrentTime();

		window.clear();
		skeleton->render(window);
		ImGui::PopFont();
		ImGui::SFML::Render(window);
		window.display();
	}

	ImGui::SFML::Shutdown();

	delete skeleton;

	return 0;
}