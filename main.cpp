#include <cstdio>
#include <cstdint>
#include <stdlib.h>
#include <cstring>
#include <GL/glew.h>
#include <GLFW/glfw3.h> 

//g++ -o main main.cpp -I/opt/homebrew/Cellar/glfw/3.3.8/include -I/opt/homebrew/Cellar/glew/2.2.0_1/include -L/opt/homebrew/Cellar/glfw/3.3.8/lib -L/opt/homebrew/Cellar/glew/2.2.0_1/lib -lglfw -lGLEW -framework OpenGL
bool game_running = false;
int mov_dir       = 0;
bool fire_pressed = false;

struct Buffer { 
    size_t width, height;
    uint32_t *data;
};

struct Sprite {
    size_t width, height;
    uint8_t *data;
};

struct Alien {
    size_t x, y;
    uint8_t type;
};

enum AlienType : uint8_t {
    ALIEN_DEAD   = 0,
    ALIEN_TYPE_A = 1,
    ALIEN_TYPE_B = 2,
    ALIEN_TYPE_C = 3,
};

struct Player {
    size_t x, y;
    size_t life;
};

struct Bullet {
    size_t x, y;
    int dir;
};


#define GAME_MAX_BULLETS 128
struct Game {
    size_t width, height;
    size_t num_aliens;
    size_t num_bullets;
    Alien *aliens;
    Player player;
    Bullet bullets[GAME_MAX_BULLETS];
};

/* 
    * A frame being showed in succesion -> Animation
*/
struct SpriteAnimation {
    bool loop;
    size_t num_frames;
    size_t frame_duration;
    size_t time;
    Sprite **frames;    // Array of pointer to the sprites rather than just the array of sprites since two frames can show the same sprite
};

/* Function to check for overlapping sprites */
bool sprite_overlap_check(const Sprite &sp_a, size_t x_a, size_t y_a,
                          const Sprite &sp_b, size_t x_b, size_t y_b) {
    if (x_a < x_b + sp_b.width && x_a + sp_a.width > x_b && 
        y_a < y_b + sp_b.height && y_a + sp_a.height > y_b) {
            return true;
        }
    return false;
}

void validate_shader(GLuint shader, const char* file = 0) {
    static const unsigned int BUFFER_SIZE = 512;
    char buffer[BUFFER_SIZE];
    GLsizei length = 0;

    glGetShaderInfoLog(shader, BUFFER_SIZE, &length, buffer);

    if (length > 0) {
        printf("Shader %d(%s) compile error: %s\n", shader, (file ? file: ""), buffer);
    }
}

bool validate_program(GLuint program) {
    static const GLsizei BUFFER_SIZE = 512;
    GLchar buffer[BUFFER_SIZE];
    GLsizei length = 0;

    glGetProgramInfoLog(program, BUFFER_SIZE, &length, buffer);

    if (length > 0) {
        printf("Program %d link error: %s\n", program, buffer);
        return false;
    }

    return true;
}

void error_callback(int error, const char * description) {
    fprintf(stderr, "Error: %s\n", description);
}

/* A callback function used to capture any user input */

void key_callback(GLFWwindow *window, int key, int scancode, int action, int mod) {
    switch(key) {
        case GLFW_KEY_ESCAPE:
            if (action == GLFW_PRESS) game_running = false;
            break;
        case GLFW_KEY_RIGHT:
            if (action == GLFW_PRESS) mov_dir += 1;
            else if (action == GLFW_RELEASE) mov_dir -= 1;
            break;
        case GLFW_KEY_LEFT:
            if (action == GLFW_PRESS) mov_dir -= 1;
            if (action == GLFW_RELEASE) mov_dir += 1;
            break;
        case GLFW_KEY_SPACE:
            if (action == GLFW_RELEASE) fire_pressed = true;  
            break;
        default:
            break;
    }
}
  
uint32_t rgb_to_uint32(uint8_t r, uint8_t g, uint8_t b) {
    return (r << 24) | (g << 16) | (b << 8) | 255;
}

//clear(set) the buffer to a certain colour
void buffer_clear(Buffer *buffer, uint32_t color) {
    for (size_t i = 0; i < buffer -> width * buffer -> height; i ++) {
        buffer -> data[i] = color;
    }
}


/*  
    * Draw the sprite in the buffer with a specified colour. Consider sprite as a bitmap, 1 being the sprite is "On" 
    ! Here moving the sprite to the location (x, y) in the buffer. 
    Bottom-most row & left-most coloumn of the sprite coincides with the (x, y). That is, sprite is built upwards and rightwards from that position. 
         
*/

void buffer_draw_sprite(Buffer* buffer, const Sprite &sprite, size_t x, size_t y, uint32_t color) {
    for (size_t xi = 0; xi < sprite.width; xi ++) {
        for (size_t yi = 0; yi < sprite.height; yi ++) {
            size_t sy = y + sprite.height - 1 - yi;
            size_t sx = x + xi;

            if (sprite.data[yi * sprite.width + xi] == 1 
                && sy < buffer -> height && sx < buffer -> width) {
                    buffer -> data[sy * buffer -> width + sx] = color;
                }
        }  
    }
}

int main(int argc, char* argv[]) {

    const size_t buffer_width = 224;
    const size_t buffer_height = 256;

    glfwSetErrorCallback(error_callback);

    if (!glfwInit()) {
        return -1;
    }

    /* Give hints to glfw for what version of OpenGL we require for the widnow/environment */
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    GLFWwindow *window = glfwCreateWindow(2* buffer_width, 2 * buffer_height, "Space Invaders", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }

        /*  Setting up a key callback */
    glfwSetKeyCallback(window, key_callback);

    glfwMakeContextCurrent(window); //create an OpenGL context for the current window
    /*
    context is the state machine that stores all the information needed to render graphics.
    (opengl version, graphic state, shaders, buffers & textures, rendering options, rendering pipeline)
    Essentially a container with information used by OpenGl api for rendering operations. 
    This context represents connection between my application and underlying graphics HW/SW & it 
    provides an environment where I can issue OpenGL commands to perform rendering

    ' ' ' '             ' ' ' ' ' '             ' ' ' ' ' ' '
    ' app '  ---------  ' OpenGL  ' ---------   ' Graphics  '
    '     '             ' context '             ' HW / SW   '
    ' ' ' '             ' ' ' ' ' '             ' ' ' ' ' ' '
    */

   /* 
      We will use GLEW loading library that will load only the OpenGL functions that are platform 
      frinedly at the runtime. Directly including OpenGL functions may cause runtime errors since OpenGL
      functions/ APIs are platform dependent.
   */

   GLenum err = glewInit();
   if (err != GLEW_OK) {
        fprintf(stderr, "Error intitalizing GLEW.\n");
        glfwTerminate();
        return -1;
   }

    int glVersion[2] = {-1, 1};
    glGetIntegerv(GL_MAJOR_VERSION, &glVersion[0]);
    glGetIntegerv(GL_MINOR_VERSION, &glVersion[1]);

    printf("Using OpenGL : %d.%d", glVersion[0], glVersion[1]);

    /* Turing On VSync*/
    glfwSwapInterval(1);
    
    /* 
        * Buffer -> 
                Stores an image inside a game-screen of (width x height) where each unit is a pixel, 
                into the RAM(CPU) for the processing. This will be then drawn to the computer screen.
        * Pixel -> 
                32bit number whose 32-bits are split as (8 | 8 | 8 | 8) = (r | g | b | alpha) where r, g, b are colours represented by 8 bit
    */

   /* create a grahics buffer */
    uint32_t clear_color = rgb_to_uint32(0, 128, 0);
    Buffer buffer;
    buffer.width  = buffer_width;
    buffer.height = buffer_height;
    buffer.data   = new uint32_t[buffer.width * buffer.height];
    buffer_clear(&buffer, clear_color); 

    /*
        * Texture - Used to tranfer image data to GPU. In case of VAO, it's also an object of VAO holding all the info on vertex
        ! Purpose being that the shader programs, which runs in GPU, use the texture to sample / look up the image it wants to render
        ! Texture holds image data(pixels) in the texture objects which are then looked up by shaders
    */

   // Create a texture for presenting buffer to OpenGL 
    GLuint buffer_texture;
    glGenTextures(1, &buffer_texture);

    glBindTexture(GL_TEXTURE_2D, buffer_texture);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGB8,
        buffer.width, buffer.height, 0,
        GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, buffer.data
    );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);        
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* 
        * Generate a vertex shader & fragment shader
        ? Shader - Methods/instructions provided to make buffer(blank canvas) show the image. 
        ? Vertex shader - arrange the elements, fragment shader - add color & design to the element
        ! written in OpenGL shading language
        ! output of vertex shader is an input for the fragment shader   
    */

   /* 
   * Creating a vao for generating a fullscreen triangle. vao is like a structure in openGl which stores format of the vertex data along with vertex data
   * It's a container for storing vertex format along with vertex data for a perticular image which can be resued again

   */
   GLuint fullscreen_triangle_vao;
   glGenVertexArrays(1, &fullscreen_triangle_vao);

    static const char* vertex_shader =
        "\n"
        "#version 330\n"
        "\n"
        "noperspective out vec2 TexCoord;\n"
        "\n"
        "void main(void){\n"
        "\n"
        "    TexCoord.x = (gl_VertexID == 2)? 2.0: 0.0;\n"
        "    TexCoord.y = (gl_VertexID == 1)? 2.0: 0.0;\n"
        "    \n"
        "    gl_Position = vec4(2.0 * TexCoord - 1.0, 0.0, 1.0);\n"
        "}\n";


    static const char* fragment_shader =
        "\n"
        "#version 330\n"
        "\n"
        "uniform sampler2D buffer;\n"
        "noperspective in vec2 TexCoord;\n"
        "\n"
        "out vec3 outColor;\n"
        "\n"
        "void main(void){\n"
        "    outColor = texture(buffer, TexCoord).rgb;\n"
        "}\n";

    /* Compile the two shaders and link them into a shader program */
    GLuint shader_id = glCreateProgram();

    // Create a vertex shader
    {
        GLuint shader_vp = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(shader_vp, 1, &vertex_shader, 0);
        glCompileShader(shader_vp);
        validate_shader(shader_vp, vertex_shader);
        glAttachShader(shader_id, shader_vp);

        glDeleteShader(shader_vp);
    }

    // Create a fragment shader
    {
        GLuint shader_fp = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(shader_fp, 1, &fragment_shader, 0);
        glCompileShader(shader_fp);
        validate_shader(shader_fp, fragment_shader);
        glAttachShader(shader_id, shader_fp);

        glDeleteShader(shader_fp);
    }
    // Link the shader program which combines the vertex and fragment shaders into a single program
    glLinkProgram(shader_id);

    if (!validate_program(shader_id)) {
        fprintf(stderr, "Error while validating shader.\n");
        glfwTerminate();
        glDeleteVertexArrays(1, &fullscreen_triangle_vao);
        delete[] buffer.data;
        return -1;
    }

    glUseProgram(shader_id);

    /* 
        This texture is attached to the unform variable "buffer" 
        ! Uniforms are used to pass data from the CPU to shader(which runs on the GPU) without changing the shader program iteslf
        ? location stores the memory location of uniform variable "buffer" in the shader program
        ? Sets this uniform with texture unit 0 which is buffer_texture as defined aboved
    */
    GLint location = glGetUniformLocation(shader_id, "buffer");
    glUniform1i(location, 0);

    //OpenGL setup
    glDisable(GL_DEPTH_TEST);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(fullscreen_triangle_vao);



    /* Creating an alien sprite */
    Sprite alien_sprites[6];

    alien_sprites[0].width = 8;
    alien_sprites[0].height = 8;
    alien_sprites[0].data = new uint8_t[64];
    uint8_t data0[64] = 
    {
        0,0,0,1,1,0,0,0, // ...@@...
        0,0,1,1,1,1,0,0, // ..@@@@..
        0,1,1,1,1,1,1,0, // .@@@@@@.
        1,1,0,1,1,0,1,1, // @@.@@.@@
        1,1,1,1,1,1,1,1, // @@@@@@@@
        0,1,0,1,1,0,1,0, // .@.@@.@.
        1,0,0,0,0,0,0,1, // @......@
        0,1,0,0,0,0,1,0  // .@....@.
    };
    memcpy(alien_sprites[0].data, data0, sizeof(data0));

    alien_sprites[1].width = 8;
    alien_sprites[1].height = 8;
    alien_sprites[1].data = new uint8_t[64];
    uint8_t data1[64] = 
    {
        0,0,0,1,1,0,0,0, // ...@@...
        0,0,1,1,1,1,0,0, // ..@@@@..
        0,1,1,1,1,1,1,0, // .@@@@@@.
        1,1,0,1,1,0,1,1, // @@.@@.@@
        1,1,1,1,1,1,1,1, // @@@@@@@@
        0,0,1,0,0,1,0,0, // ..@..@..
        0,1,0,1,1,0,1,0, // .@.@@.@.
        1,0,1,0,0,1,0,1  // @.@..@.@
    };
    memcpy(alien_sprites[1].data, data1, sizeof(data1));

    alien_sprites[2].width = 11;
    alien_sprites[2].height = 8;
    alien_sprites[2].data = new uint8_t[88];
    uint8_t data2[88] = 
    {
        0,0,1,0,0,0,0,0,1,0,0, // ..@.....@..
        0,0,0,1,0,0,0,1,0,0,0, // ...@...@...
        0,0,1,1,1,1,1,1,1,0,0, // ..@@@@@@@..
        0,1,1,0,1,1,1,0,1,1,0, // .@@.@@@.@@.
        1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@
        1,0,1,1,1,1,1,1,1,0,1, // @.@@@@@@@.@
        1,0,1,0,0,0,0,0,1,0,1, // @.@.....@.@
        0,0,0,1,1,0,1,1,0,0,0  // ...@@.@@...
    };
    memcpy(alien_sprites[2].data, data2, sizeof(data2));

    alien_sprites[3].width = 11;
    alien_sprites[3].height = 8;
    alien_sprites[3].data = new uint8_t[88];
    uint8_t data3[88] = 
    {
        0,0,1,0,0,0,0,0,1,0,0, // ..@.....@..
        1,0,0,1,0,0,0,1,0,0,1, // @..@...@..@
        1,0,1,1,1,1,1,1,1,0,1, // @.@@@@@@@.@
        1,1,1,0,1,1,1,0,1,1,1, // @@@.@@@.@@@
        1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@
        0,1,1,1,1,1,1,1,1,1,0, // .@@@@@@@@@.
        0,0,1,0,0,0,0,0,1,0,0, // ..@.....@..
        0,1,0,0,0,0,0,0,0,1,0  // .@.......@.
    };
    memcpy(alien_sprites[3].data, data3, sizeof(data3));

    alien_sprites[4].width = 12;
    alien_sprites[4].height = 8;
    alien_sprites[4].data = new uint8_t[96];
    uint8_t data4[96] = 
    {
        0,0,0,0,1,1,1,1,0,0,0,0, // ....@@@@....
        0,1,1,1,1,1,1,1,1,1,1,0, // .@@@@@@@@@@.
        1,1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@@
        1,1,1,0,0,1,1,0,0,1,1,1, // @@@..@@..@@@
        1,1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@@
        0,0,0,1,1,0,0,1,1,0,0,0, // ...@@..@@...
        0,0,1,1,0,1,1,0,1,1,0,0, // ..@@.@@.@@..
        1,1,0,0,0,0,0,0,0,0,1,1  // @@........@@
    };
    memcpy(alien_sprites[4].data, data4, sizeof(data4));


    alien_sprites[5].width = 12;
    alien_sprites[5].height = 8;
    alien_sprites[5].data = new uint8_t[96];
    uint8_t data5[96] = 
    {
        0,0,0,0,1,1,1,1,0,0,0,0, // ....@@@@....
        0,1,1,1,1,1,1,1,1,1,1,0, // .@@@@@@@@@@.
        1,1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@@
        1,1,1,0,0,1,1,0,0,1,1,1, // @@@..@@..@@@
        1,1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@@
        0,0,1,1,1,0,0,1,1,1,0,0, // ..@@@..@@@..
        0,1,1,0,0,1,1,0,0,1,1,0, // .@@..@@..@@.
        0,0,1,1,0,0,0,0,1,1,0,0  // ..@@....@@..
    };
    memcpy(alien_sprites[5].data, data5, sizeof(data5));

    Sprite alien_death_sprite;
    alien_death_sprite.width = 13;
    alien_death_sprite.height = 7;
    alien_death_sprite.data = new uint8_t[91];
    uint8_t data_death_sprite[91] = 
    {
        0,1,0,0,1,0,0,0,1,0,0,1,0, // .@..@...@..@.
        0,0,1,0,0,1,0,1,0,0,1,0,0, // ..@..@.@..@..
        0,0,0,1,0,0,0,0,0,1,0,0,0, // ...@.....@...
        1,1,0,0,0,0,0,0,0,0,0,1,1, // @@.........@@
        0,0,0,1,0,0,0,0,0,1,0,0,0, // ...@.....@...
        0,0,1,0,0,1,0,1,0,0,1,0,0, // ..@..@.@..@..
        0,1,0,0,1,0,0,0,1,0,0,1,0  // .@..@...@..@.
    };
    memcpy(alien_death_sprite.data, data_death_sprite, sizeof(data_death_sprite));

    /* Creating a player sprite */
    Sprite player_sprite;
    player_sprite.width  = 11;
    player_sprite.height = 7;
    player_sprite.data = new uint8_t[77];

    uint8_t player_data[77] = 
    {
        0,0,0,0,0,1,0,0,0,0,0, // .....@.....
        0,0,0,0,1,1,1,0,0,0,0, // ....@@@....
        0,0,0,0,1,1,1,0,0,0,0, // ....@@@....
        0,1,1,1,1,1,1,1,1,1,0, // .@@@@@@@@@.
        1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@
        1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@
        1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@
    };

    std::memcpy(player_sprite.data, player_data, sizeof(player_data));

    /* Sprite for a bullet */
    Sprite bullet_sprite;
    bullet_sprite.width = 1;
    bullet_sprite.height = 3;
    bullet_sprite.data = new uint8_t[3];

    uint8_t bullet_data[3] = 
    {
        1, 
        1,
        1
    };
    std::memcpy(bullet_sprite.data, bullet_data, sizeof(bullet_data));

    /* 
        * Create an Alien Animation
        ! Total three types of aliens and each alien has two-frame animation
    */
    SpriteAnimation alien_animation[3];

    for (int i = 0; i < 3; i ++) {
        alien_animation[i].loop = true;
        alien_animation[i].num_frames = 2;
        alien_animation[i].frame_duration = 10;
        alien_animation[i].time = 0;
        alien_animation[i].frames = new Sprite*[2];
        alien_animation[i].frames[0] = &alien_sprites[2 * i];
        alien_animation[i].frames[1] = &alien_sprites[2 * i + 1];
    }


    /* Create a Game struct */
    Game game;
    game.width  = buffer_width;
    game.height = buffer_height;
    game.num_bullets = 0;
    game.num_aliens = 55;
    game.aliens = new Alien[game.num_aliens];
    game.player.life = 3;
    game.player.x = 112 - 5;
    game.player.y = 32;

    /* Keeping track of alien deaths */
    uint8_t *death_counters = new uint8_t[game.num_aliens];
    for (size_t i = 0; i < game.num_aliens; i ++) {
        death_counters[i] = 10;
    }

    /* fill alien positions */
    
    for (size_t yi = 0; yi < 5; ++yi) {
        for (size_t xi = 0; xi < 11; ++xi) {
            Alien &alien = game.aliens[yi * 11 + xi];
            alien.type   = (5 - yi) / 2 + 1;

            const Sprite& sprite = alien_sprites[2 * (alien.type - 1)];

            alien.x = 16 * xi + 20 + (alien_death_sprite.width - sprite.width) / 2;
            alien.y = 17 * yi + 128;
        }
    }

    /*
        Game Loop - infinite loop where input in processed and game is updated & drawn. 
        Basically the heart of every game, otherwise the game program will never run.
    */
    game_running = true;

    while (!glfwWindowShouldClose(window) && game_running) {

        buffer_clear(&buffer, clear_color);

        // Draw

        for (size_t ai = 0; ai < game.num_aliens; ai ++) {
            if (!death_counters[ai]) continue;

            const Alien &alien = game.aliens[ai];

            if (alien.type == ALIEN_DEAD) {
                buffer_draw_sprite(&buffer, alien_death_sprite, 
                                    alien.x, alien.y, 
                                    rgb_to_uint32(128, 0, 0));
            }
            else {
                const SpriteAnimation &animation = alien_animation[alien.type - 1];
                size_t current_frame = animation.time / animation.frame_duration;
                const Sprite &sprite = *animation.frames[current_frame]; 
                buffer_draw_sprite(&buffer, sprite, 
                                    alien.x, alien.y, 
                                    rgb_to_uint32(128, 0, 0));
            }
        }

        buffer_draw_sprite(&buffer, player_sprite,
                            game.player.x, game.player.y, 
                            rgb_to_uint32(128, 0, 0));
        
        for (size_t bi = 0; bi < game.num_bullets; bi ++) {
            const Bullet &bullet = game.bullets[bi];
            const Sprite &sprite = bullet_sprite;
            buffer_draw_sprite(&buffer, sprite, 
                                bullet.x, bullet.y, 
                                rgb_to_uint32(128, 0, 0));
        }
        
        /* Updating animation */
        for (int i = 0; i < 3; i ++) {
            ++alien_animation[i].time;
            if (alien_animation[i].time == alien_animation[i].num_frames * alien_animation[i].frame_duration) {
                alien_animation[i].time = 0;
            }
        }
    
        glTexSubImage2D(
            GL_TEXTURE_2D, 0, 0, 0,
            buffer.width, buffer.height,
            GL_RGBA, GL_UNSIGNED_INT_8_8_8_8,
            buffer.data
        );

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glfwSwapBuffers(window);

        /* Simulate aliens*/
        for (size_t ai = 0; ai < game.num_aliens; ++ai) {
            const Alien& alien = game.aliens[ai];
            if (alien.type == ALIEN_DEAD && death_counters[ai]) {
                --death_counters[ai];
            }
        }

        /* Simulate player */
        int player_mov_dir = 2 * mov_dir;
        if (player_mov_dir != 0) {
            if (game.player.x + player_sprite.width + player_mov_dir >= game.width - 1) {
                game.player.x = game.width - player_sprite.width;
            }
            else if ((int)game.player.x + player_mov_dir <= 0) {
                game.player.x = 0;
            }
            else game.player.x += player_mov_dir;
        }

        /* Simulate Bullets */
        for (size_t bi = 0; bi <game.num_bullets; ) {
            game.bullets[bi].y += game.bullets[bi].dir;
            if (game.bullets[bi].y >= game.height || game.bullets[bi].y < bullet_sprite.height) {
                game.bullets[bi] = game.bullets[game.num_bullets - 1];
                -- game.num_bullets;
                continue;
            }

            // Check if bullet has hit the alien
            for (size_t ai = 0; ai < game.num_aliens; ++ai) {
                const Alien &alien = game.aliens[ai];
                if (alien.type == ALIEN_DEAD) 
                    continue;
                
                const SpriteAnimation &animation = alien_animation[alien.type - 1];
                size_t current_frame = animation.time / animation.frame_duration;
                const Sprite& alien_sprite = *animation.frames[current_frame];

                bool overlap = sprite_overlap_check(bullet_sprite, game.bullets[bi].x, game.bullets[bi].y,
                                            alien_sprite, alien.x, alien.y);
                if (overlap) {
                    score += 10 * (4 - game.aliens[ai].type);
                    game.aliens[ai].type = ALIEN_DEAD;
                    game.aliens[ai].x -= (alien_death_sprite.width - alien_sprite.width) / 2;
                    game.bullets[bi] = game.bullets[game.num_bullets - 1];
                    --game.num_bullets;
                    continue;
                }

            }
            ++ bi;
        } 

        // Process Events
        if (fire_pressed && game.num_bullets < GAME_MAX_BULLETS) {
            game.bullets[game.num_bullets].x   = game.player.x + player_sprite.width / 2;
            game.bullets[game.num_bullets].y   = game.player.y + player_sprite.height;
            game.bullets[game.num_bullets].dir = 2;
            ++ game.num_bullets;
        }
        fire_pressed = false;

        glfwPollEvents();   
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    
    glDeleteVertexArrays(1, &fullscreen_triangle_vao);

    for(size_t i = 0; i < 6; ++i)
    {
        delete[] alien_sprites[i].data;
    }

    delete[] alien_death_sprite.data;

    for(size_t i = 0; i < 3; ++i)
    {
        delete[] alien_animation[i].frames;
    }
    delete[] buffer.data;
    delete[] game.aliens;
    delete[] death_counters;

    return 0;
}