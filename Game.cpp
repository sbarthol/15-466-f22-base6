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

Player *Game::spawn_player() {
	if(!gun_spawned) {
		gun_spawned = true;
		return &gun;
	} else if(!chicken_spawned) {
		chicken_spawned = true;
		return &chicken;
	} else {
		return nullptr;
	}
}

void Game::remove_player(Player *player) {
  bool found = false;
	std::vector<Player> players{gun, chicken};
  for (auto pi = players.begin(); pi != players.end(); ++pi) {
    if (&*pi == player) {
      players.erase(pi);
      found = true;
      break;
    }
  }
  assert(found);
}

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

Game::Game() : mt(0x15466666) {}


void Game::update(float elapsed) {
	//move camera:
	{

		//combine inputs into a move:
		constexpr float GunSpeed = 2.5f;
		glm::vec2 move = glm::vec2(0.0f);
		if (gun.controls.left.pressed && !gun.controls.right.pressed) move.x =-1.0f;
		if (!gun.controls.left.pressed && gun.controls.right.pressed) move.x = 1.0f;
		if (gun.controls.down.pressed && !gun.controls.up.pressed) move.y =-1.0f;
		if (!gun.controls.down.pressed && gun.controls.up.pressed) move.y = 1.0f;

		//make it so that moving diagonally doesn't go faster:
		if (move != glm::vec2(0.0f)) move = glm::normalize(move) * GunSpeed * elapsed;

		gun.position.x += move.x;
		gun.position.z += move.y;

		//reset button press counters:
		gun.controls.left.downs = 0;
		gun.controls.right.downs = 0;
		gun.controls.up.downs = 0;
		gun.controls.down.downs = 0;
		gun.controls.space_pressed = false;
	}

	{ // fire gun
		if (gun.controls.space_pressed) {
			gun.gun_fired = true;
		}
	}

	{ // move chicken
		glm::vec2 move = glm::vec2(0.0f);
		if (chicken.controls.left.pressed && !chicken.controls.right.pressed) move.x =-1.0f;
		if (!chicken.controls.left.pressed && chicken.controls.right.pressed) move.x = 1.0f;
		if (chicken.controls.down.pressed && !chicken.controls.up.pressed) move.y =-1.0f;
		if (!chicken.controls.down.pressed && chicken.controls.up.pressed) move.y = 1.0f;

		//make it so that moving diagonally doesn't go faster:
		constexpr float ChickenSpeed = 13.f;
		if (move != glm::vec2(0.0f)) move = glm::normalize(move) * ChickenSpeed * elapsed;

		chicken.position.x += move.x;
		chicken.position.z += move.y;

		//reset button press counters:
		chicken.controls.left.downs = 0;
		chicken.controls.right.downs = 0;
		chicken.controls.up.downs = 0;
		chicken.controls.down.downs = 0;
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
		connection.send(player.gun_fired);
  };

  // player count:
  for (auto const &player : {gun, chicken}) {
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

	read(&(gun.position));
	read(&(gun.gun_fired));
  read(&(chicken.position));
	read(&(chicken.gun_fired));
  
  if (at != size) throw std::runtime_error("Trailing data in state message.");

  // delete message from buffer:
  recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

  return true;
}
