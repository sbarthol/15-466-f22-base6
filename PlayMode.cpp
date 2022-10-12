#include "PlayMode.hpp"

#include <array>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>
#include <random>

#include "DrawLines.hpp"
#include "LitColorTextureProgram.hpp"
#include "Load.hpp"
#include "Mesh.hpp"
#include "Sound.hpp"
#include "data_path.hpp"
#include "gl_errors.hpp"
#include "hex_dump.hpp"

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

void PlayMode::fire_gun() {
  Mesh const &mesh = chicken_meshes->lookup("Impact");

  Scene::Transform *transform = new Scene::Transform();
  transform->position =
      glm::vec3(gun->position.x, impact->position.y,
                gun->position.z - 3);
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

PlayMode::~PlayMode() {}

PlayMode::PlayMode(Client &client_) : scene(*chicken_scene), client(client_) {
  // get pointers to leg for convenience:
  for (auto &transform : scene.transforms) {
    if (transform.name == "Chicken")
      {
				chicken = &transform;
			}
			else if (transform.name == "Gun") {
				gun = &transform;
			}
    
    else if (transform.name == "Wall")
      wall = &transform;
    else if (transform.name == "Impact")
      impact = &transform;
  }


  // get pointer to camera for convenience:
  if (scene.cameras.size() != 1)
    throw std::runtime_error(
        "Expecting scene to have exactly one camera, but it has " +
        std::to_string(scene.cameras.size()));
  camera = &scene.cameras.front();

}

bool PlayMode::handle_event(SDL_Event const &evt,
                            glm::uvec2 const &window_size) {
  if (evt.type == SDL_KEYDOWN) {
    if (evt.key.repeat) {
      // ignore repeats
    } else if (evt.key.keysym.sym == SDLK_a) {
      controls.left.downs += 1;
      controls.left.pressed = true;
      return true;
    } else if (evt.key.keysym.sym == SDLK_d) {
      controls.right.downs += 1;
      controls.right.pressed = true;
      return true;
    } else if (evt.key.keysym.sym == SDLK_w) {
      controls.up.downs += 1;
      controls.up.pressed = true;
      return true;
    } else if (evt.key.keysym.sym == SDLK_s) {
      controls.down.downs += 1;
      controls.down.pressed = true;
      return true;
    } else if (evt.key.keysym.sym == SDLK_SPACE) {
      controls.jump.downs += 1;
      controls.jump.pressed = true;
      return true;
    }
  } else if (evt.type == SDL_KEYUP) {
    if (evt.key.keysym.sym == SDLK_a) {
      controls.left.pressed = false;
      return true;
    } else if (evt.key.keysym.sym == SDLK_d) {
      controls.right.pressed = false;
      return true;
    } else if (evt.key.keysym.sym == SDLK_w) {
      controls.up.pressed = false;
      return true;
    } else if (evt.key.keysym.sym == SDLK_s) {
      controls.down.pressed = false;
      return true;
    } else if (evt.key.keysym.sym == SDLK_SPACE) {
      controls.jump.pressed = false;
      return true;
    }
  }

  return false;
}

void PlayMode::update(float elapsed) {
  // queue data for sending to server:
  controls.send_controls_message(&client.connection);

  // reset button press counters:
  controls.left.downs = 0;
  controls.right.downs = 0;
  controls.up.downs = 0;
  controls.down.downs = 0;
  controls.jump.downs = 0;

  // send/receive data:
  client.poll(
      [this](Connection *c, Connection::Event event) {
        if (event == Connection::OnOpen) {
          std::cout << "[" << c->socket << "] opened" << std::endl;
        } else if (event == Connection::OnClose) {
          std::cout << "[" << c->socket << "] closed (!)" << std::endl;
          throw std::runtime_error("Lost connection to server!");
        } else {
          assert(event == Connection::OnRecv);
          // std::cout << "[" << c->socket << "] recv'd data. Current buffer:\n"
          // << hex_dump(c->recv_buffer); std::cout.flush(); //DEBUG
          bool handled_message;
          try {
            do {
              handled_message = false;
              if (game.recv_state_message(c)) handled_message = true;
            } while (handled_message);
          } catch (std::exception const &e) {
            std::cerr << "[" << c->socket
                      << "] malformed message from server: " << e.what()
                      << std::endl;
            // quit the game:
            throw e;
          }


          // move camera:
          {
            /*glm::mat4x3 frame = camera->transform->make_local_to_parent();
            glm::vec3 frame_right = frame[0];
            glm::vec3 frame_up = frame[1];*/
            // glm::vec3 frame_forward = -frame[2];

            gun->position = game.gun.position;
          }

          {  // update listener to camera position:
            glm::mat4x3 frame = camera->transform->make_local_to_parent();
            glm::vec3 frame_right = frame[0];
            glm::vec3 frame_at = frame[3];
            Sound::listener.set_position_right(frame_at, frame_right,
                                               1.0f / 60.0f);
          }

          {  // fire gun
            if (game.gun.gun_fired) {
              fire_gun();
            }
          }

          {  // move chicken
            chicken->position = game.chicken.position;
          }
        }
      },
      0.0);
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
  // update camera aspect ratio for drawable:
  camera->aspect = float(drawable_size.x) / float(drawable_size.y);

  // set up light type and position for lit_color_texture_program:
  glUseProgram(lit_color_texture_program->program);
  glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
  glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1,
               glm::value_ptr(glm::vec3(0.0f, 0.0f, -1.0f)));
  glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1,
               glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
  glUseProgram(0);

  glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
  glClearDepth(1.0f);  // 1.0 is actually the default value to clear the depth
                       // buffer to, but FYI you can change it.
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);  // this is the default depth comparison function, but
                         // FYI you can change it.

  scene.draw(*camera);

  {  // use DrawLines to overlay some text:
    glDisable(GL_DEPTH_TEST);
    float aspect = float(drawable_size.x) / float(drawable_size.y);
    DrawLines lines(glm::mat4(1.0f / aspect, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                              0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                              1.0f));

    constexpr float H = 0.09f;
    lines.draw_text("WASD moves player; space fires the gun;",
                    glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H, 0.0),
                    glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
                    glm::u8vec4(0x00, 0x00, 0x00, 0x00));
    float ofs = 2.0f / drawable_size.y;
    lines.draw_text(
        "WASD moves player; space fires the gun;",
        glm::vec3(-aspect + 0.1f * H + ofs, -1.0 + +0.1f * H + ofs, 0.0),
        glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
        glm::u8vec4(0xff, 0xff, 0xff, 0x00));

    lines.draw_text("Shots: " + std::to_string(gunshots),
                    glm::vec3(-aspect + 0.1f * H, 0.9 - 0.1f * H, 0.0),
                    glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
                    glm::u8vec4(0x00, 0x00, 0x00, 0x00));
    lines.draw_text(
        "Shots: " + std::to_string(gunshots),
        glm::vec3(-aspect + 0.1f * H + ofs, 0.9 - 0.1f * H + ofs, 0.0),
        glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
        glm::u8vec4(0xff, 0xff, 0xff, 0x00));

    lines.draw_text("Hits: " + std::to_string(hits),
                    glm::vec3(-aspect + 0.1f * H, 0.77 - 0.1f * H, 0.0),
                    glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
                    glm::u8vec4(0x00, 0x00, 0x00, 0x00));
    lines.draw_text(
        "Hits: " + std::to_string(hits),
        glm::vec3(-aspect + 0.1f * H + ofs, 0.77 - 0.1f * H + ofs, 0.0),
        glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
        glm::u8vec4(0xff, 0xff, 0xff, 0x00));
  }
  GL_ERRORS();
}
