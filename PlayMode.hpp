#include "Mode.hpp"

#include "Connection.hpp"
#include "Game.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>

struct PlayMode : Mode {
	PlayMode(Client &client);
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	//input tracking for local player:
	Player::Controls controls;

	//latest game state (from server):
	Game game;

	Scene::Transform *chicken = nullptr;
	Scene::Transform *gun = nullptr;
	Scene::Transform *wall = nullptr;
	Scene::Transform *impact = nullptr;

	// angle between 0 and 360 degrees,
	// mathematical
	size_t hits = 0;
	size_t gunshots = 0;

	//camera:
	Scene::Camera *camera = nullptr;

	//local copy of the game scene (so code can change it during gameplay):
	Scene scene;

	void fire_gun();

	//last message from server:
	std::string server_message;

	//connection to server:
	Client &client;

};
