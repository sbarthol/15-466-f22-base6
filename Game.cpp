#include "Game.hpp"

#include <cstring>
#include <glm/gtx/norm.hpp>
#include <iostream>
#include <stdexcept>

#include "Connection.hpp"
#include "LitColorTextureProgram.hpp"
#include "Load.hpp"
#include "Mesh.hpp"
#include "data_path.hpp"

void Player::Controls::send_controls_message(Connection *connection_) const {
  assert(connection_);
  auto &connection = *connection_;

  uint32_t size = 5;
  connection.send(Message::C2S_Controls);
  connection.send(uint8_t(size));
  connection.send(uint8_t(size >> 8));
  connection.send(uint8_t(size >> 16));

  auto send_button = [&](Button const &b) {
    if (b.downs & 0x80) {
      std::cerr << "Wow, you are really good at pressing buttons!" << std::endl;
    }
    connection.send(uint8_t((b.pressed ? 0x80 : 0x00) | (b.downs & 0x7f)));
  };

  send_button(left);
  send_button(right);
  send_button(up);
  send_button(down);
  send_button(jump);
}

bool Player::Controls::recv_controls_message(Connection *connection_) {
  assert(connection_);
  auto &connection = *connection_;

  auto &recv_buffer = connection.recv_buffer;

  // expecting [type, size_low0, size_mid8, size_high8]:
  if (recv_buffer.size() < 4) return false;
  if (recv_buffer[0] != uint8_t(Message::C2S_Controls)) return false;
  uint32_t size = (uint32_t(recv_buffer[3]) << 16) |
                  (uint32_t(recv_buffer[2]) << 8) | uint32_t(recv_buffer[1]);
  if (size != 5)
    throw std::runtime_error("Controls message with size " +
                             std::to_string(size) + " != 5!");

  // expecting complete message:
  if (recv_buffer.size() < 4 + size) return false;

  auto recv_button = [](uint8_t byte, Button *button) {
    button->pressed = (byte & 0x80);
    uint32_t d = uint32_t(button->downs) + uint32_t(byte & 0x7f);
    if (d > 255) {
      std::cerr << "got a whole lot of downs" << std::endl;
      d = 255;
    }
    button->downs = uint8_t(d);
  };

  recv_button(recv_buffer[4 + 0], &left);
  recv_button(recv_buffer[4 + 1], &right);
  recv_button(recv_buffer[4 + 2], &up);
  recv_button(recv_buffer[4 + 3], &down);
  recv_button(recv_buffer[4 + 4], &jump);

  // delete message from buffer:
  recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

  return true;
}

//-----------------------------------------

GLuint chicken_meshes_for_lit_color_texture_program = 0;
Load<MeshBuffer> chicken_meshes(LoadTagDefault, []() -> MeshBuffer const * {
  MeshBuffer const *ret = new MeshBuffer(data_path("chicken.pnct"));
  chicken_meshes_for_lit_color_texture_program =
      ret->make_vao_for_program(lit_color_texture_program->program);
  return ret;
});

Load<Sound::Sample> explosion_sample(LoadTagDefault,
                                     []() -> Sound::Sample const * {
                                       // Created on
                                       // https://jfxr.frozenfractal.com/ by
                                       // taking inspiration from the
                                       // "explosion" template
                                       return new Sound::Sample(
                                           data_path("explosion.wav"));
                                     });

Load<Sound::Sample> hit_sample(LoadTagDefault, []() -> Sound::Sample const * {
  // Created on https://jfxr.frozenfractal.com/ by taking inspiration from the
  // "hit/hurt" template
  return new Sound::Sample(data_path("hit.wav"));
});

Load<Scene> chicken_scene(LoadTagDefault, []() -> Scene const * {
  return new Scene(
      data_path("chicken.scene"), [&](Scene &scene, Scene::Transform *transform,
                                      std::string const &mesh_name) {
        if (mesh_name == "Impact") {
          return;
        }
        Mesh const &mesh = chicken_meshes->lookup(mesh_name);

        scene.drawables.emplace_back(transform);
        Scene::Drawable &drawable = scene.drawables.back();

        drawable.pipeline = lit_color_texture_program_pipeline;

        drawable.pipeline.vao = chicken_meshes_for_lit_color_texture_program;
        drawable.pipeline.type = mesh.type;
        drawable.pipeline.start = mesh.start;
        drawable.pipeline.count = mesh.count;
      });
});

float dist_sqr(float x1, float y1, float x2, float y2) {
  return (x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2);
}

void Game::fire_gun() {
  Mesh const &mesh = chicken_meshes->lookup("Impact");

  Scene::Transform *transform = new Scene::Transform();
  transform->position =
      glm::vec3(camera->transform->position.x, impact->position.y,
                camera->transform->position.z - 3);
  transform->scale = impact->scale;
  transform->rotation = impact->rotation;

  scene.drawables.emplace_back(Scene::Drawable(transform));
  Scene::Drawable &drawable = scene.drawables.back();

  drawable.pipeline = lit_color_texture_program_pipeline;

  drawable.pipeline.vao = chicken_meshes_for_lit_color_texture_program;
  drawable.pipeline.type = mesh.type;
  drawable.pipeline.start = mesh.start;
  drawable.pipeline.count = mesh.count;

  // check for hit
  if (dist_sqr(transform->position.x, transform->position.z,
               chicken->position.x, chicken->position.z) < 0.5f) {
    hits++;
    Sound::play(*hit_sample);
  }
  Sound::play(*explosion_sample);
  gunshots++;
}

Load<Sound::Sample> dusty_floor_sample(LoadTagDefault,
                                       []() -> Sound::Sample const * {
                                         return new Sound::Sample(
                                             data_path("dusty-floor.opus"));
                                       });

Game::Game() : mt(0x15466666), scene(*chicken_scene) {}

Player *Game::spawn_player() {
  if (players.size() == 0) {
    players.emplace_back();
    Player &player = players.back();

    // random point in the middle area of the arena:
		player.position = gun->position;
    player.name = "Gun";
    return &player;

  } else if (players.size() == 1) {
    players.emplace_back();
    Player &player = players.back();

    // random point in the middle area of the arena:
		player.position = gun->position;
    player.name = "Gun";
    return &player;

  } else {
    assert(false);
  }
}

void Game::remove_player(Player *player) {
  bool found = false;
  for (auto pi = players.begin(); pi != players.end(); ++pi) {
    if (&*pi == player) {
      players.erase(pi);
      found = true;
      break;
    }
  }
  assert(found);
}

void Game::update(float elapsed) {
	//move camera:
	{

		//combine inputs into a move:
		constexpr float PlayerSpeed = 2.5f;
		Player gun_player = players.front();
		assert(gun_player.name == "Gun");
		glm::vec2 move = glm::vec2(0.0f);
		if (gun_player.controls.left.pressed && !gun_player.controls.right.pressed) move.x =-1.0f;
		if (!gun_player.controls.left.pressed && gun_player.controls.right.pressed) move.x = 1.0f;
		if (gun_player.controls.down.pressed && !gun_player.controls.up.pressed) move.y =-1.0f;
		if (!gun_player.controls.down.pressed && gun_player.controls.up.pressed) move.y = 1.0f;

		//make it so that moving diagonally doesn't go faster:
		if (move != glm::vec2(0.0f)) move = glm::normalize(move) * PlayerSpeed * elapsed;

		glm::mat4x3 frame = camera->transform->make_local_to_parent();
		glm::vec3 frame_right = frame[0];
		glm::vec3 frame_up = frame[1];
		//glm::vec3 frame_forward = -frame[2];

		camera->transform->position += move.x * frame_right + move.y * frame_up;

		//reset button press counters:
		gun_player.controls.left.downs = 0;
		gun_player.controls.right.downs = 0;
		gun_player.controls.up.downs = 0;
		gun_player.controls.down.downs = 0;
		gun_player.controls.space_pressed = false;
	}

	{ //update listener to camera position:
		glm::mat4x3 frame = camera->transform->make_local_to_parent();
		glm::vec3 frame_right = frame[0];
		glm::vec3 frame_at = frame[3];
		Sound::listener.set_position_right(frame_at, frame_right, 1.0f / 60.0f);
	}

	{ // fire gun
		Player gun_player = players.front();
		if (gun_player.controls.space_pressed) {
			fire_gun();
		}
	}

	{ // move chicken
		Player chicken_player = players.back();
		assert(chicken_player.name == "Chicken");
		glm::vec2 move = glm::vec2(0.0f);
		if (chicken_player.controls.left.pressed && !chicken_player.controls.right.pressed) move.x =-1.0f;
		if (!chicken_player.controls.left.pressed && chicken_player.controls.right.pressed) move.x = 1.0f;
		if (chicken_player.controls.down.pressed && !chicken_player.controls.up.pressed) move.y =-1.0f;
		if (!chicken_player.controls.down.pressed && chicken_player.controls.up.pressed) move.y = 1.0f;

		//make it so that moving diagonally doesn't go faster:
		constexpr float ChickenSpeed = 13.f;
		if (move != glm::vec2(0.0f)) move = glm::normalize(move) * ChickenSpeed * elapsed;

		chicken->position.x += move.x;
		chicken->position.z += move.y;

		//reset button press counters:
		chicken_player.controls.left.downs = 0;
		chicken_player.controls.right.downs = 0;
		chicken_player.controls.up.downs = 0;
		chicken_player.controls.down.downs = 0;
	}
}

void Game::send_state_message(Connection *connection_,
                              Player *connection_player) const {
  assert(connection_);
  auto &connection = *connection_;

  connection.send(Message::S2C_State);
  // will patch message size in later, for now placeholder bytes:
  connection.send(uint8_t(0));
  connection.send(uint8_t(0));
  connection.send(uint8_t(0));
  size_t mark = connection.send_buffer
                    .size();  // keep track of this position in the buffer

  // send player info helper:
  auto send_player = [&](Player const &player) {
    connection.send(player.position);
  };

  // player count:
  if (connection_player) send_player(*connection_player);
  for (auto const &player : players) {
    if (&player == connection_player) continue;
    send_player(player);
  }

  // compute the message size and patch into the message header:
  uint32_t size = uint32_t(connection.send_buffer.size() - mark);
  connection.send_buffer[mark - 3] = uint8_t(size);
  connection.send_buffer[mark - 2] = uint8_t(size >> 8);
  connection.send_buffer[mark - 1] = uint8_t(size >> 16);
}

bool Game::recv_state_message(Connection *connection_) {
  assert(connection_);
  auto &connection = *connection_;
  auto &recv_buffer = connection.recv_buffer;

  if (recv_buffer.size() < 4) return false;
  if (recv_buffer[0] != uint8_t(Message::S2C_State)) return false;
  uint32_t size = (uint32_t(recv_buffer[3]) << 16) |
                  (uint32_t(recv_buffer[2]) << 8) | uint32_t(recv_buffer[1]);
  uint32_t at = 0;
  // expecting complete message:
  if (recv_buffer.size() < 4 + size) return false;

  // copy bytes from buffer and advance position:
  auto read = [&](auto *val) {
    if (at + sizeof(*val) > size) {
      throw std::runtime_error("Ran out of bytes reading state message.");
    }
    std::memcpy(val, &recv_buffer[4 + at], sizeof(*val));
    at += sizeof(*val);
  };

  read(&(chicken->position));
  read(&(gun->position));

  if (at != size) throw std::runtime_error("Trailing data in state message.");

  // delete message from buffer:
  recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

  return true;
}
