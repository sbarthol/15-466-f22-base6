#pragma once

#include <glm/glm.hpp>

#include <string>
#include <list>
#include <random>

#include "Scene.hpp"
#include "Sound.hpp"

struct Connection;

//Game state, separate from rendering.

//Currently set up for a "client sends controls" / "server sends whole state" situation.

enum class Message : uint8_t {
	C2S_Controls = 1, //Greg!
	S2C_State = 's',
	//...
};

//used to represent a control input:
struct Button {
	uint8_t downs = 0; //times the button has been pressed
	bool pressed = false; //is the button pressed now
};

//state of one player in the game:
struct Player {
	//player inputs (sent from client):
	struct Controls {
		Button left, right, up, down, jump;
		bool space_pressed = false;

		void send_controls_message(Connection *connection) const;

		//returns 'false' if no message or not a controls message,
		//returns 'true' if read a controls message,
		//throws on malformed controls message
		bool recv_controls_message(Connection *connection);
	} controls;

	//player state (sent from server):
	glm::vec3 position = glm::vec3(0.0f);
	std::string name = "";
};

struct Game {
	std::list< Player > players; //(using list so they can have stable addresses)

	Player *spawn_player(); //add player the end of the players list (may also, e.g., play some spawn anim)
	void remove_player(Player *); //remove player from game (may also, e.g., play some despawn anim)

	std::mt19937 mt; //used for spawning players
	uint32_t next_player_number = 1; //used for naming players

	Game();

	//state update function:
	void update(float elapsed);

	//constants:
	//the update rate on the server:
	inline static constexpr float Tick = 1.0f / 30.0f;

	Scene::Transform *chicken = nullptr;
	Scene::Transform *gun = nullptr;
	Scene::Transform *wall = nullptr;
	Scene::Transform *impact = nullptr;

	// angle between 0 and 360 degrees,
	// mathematical
	size_t chicken_dir = 0;
	size_t hits = 0;
	size_t gunshots = 0;

	//camera:
	Scene::Camera *camera = nullptr;

	//local copy of the game scene (so code can change it during gameplay):
	Scene scene;

	void fire_gun();
	

	//---- communication helpers ----

	//used by client:
	//set game state from data in connection buffer
	// (return true if data was read)
	bool recv_state_message(Connection *connection);

	//used by server:
	//send game state.
	//  Will move "connection_player" to the front of the front of the sent list.
	void send_state_message(Connection *connection, Player *connection_player = nullptr) const;
};
