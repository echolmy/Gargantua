#include <assert.h>
#include <iostream>
#include <map>
#include <stdio.h>
#include <vector>
#define _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#include <cmath>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <imgui.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
// #include "GLDebugMessageCallback.h"
// #include "imgui_impl_glfw.h"
// #include "imgui_impl_opengl3.h"
// #include "render.h"
// #include "shader.h"
// #include "texture.h"

static float mouseX, mouseY;
bool Gravity = false;
double G = 6.67430e-11; // Newtonian constant of gravitation
double c = 299792458.0; // speed of light
static const float kPi = glm::pi<float>();

static std::string GetExecutableDir()
{
#if defined(_WIN32)
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
    {
        return ".";
    }
    std::string path(buf, len);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    _NSGetExecutablePath(buf.data(), &size);
    std::string path = buf;
#else
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf));
    std::string path = std::string(buf, (len > 0 ? (size_t)len : 0));
#endif
    const auto pos = path.find_last_of("/\\");
    if (pos == std::string::npos)
    {
        return ".";
    }
    return path.substr(0, pos);
}

static std::string ReadTextFile(const std::string &p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot open file: " + p);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

class Camera
{
public:
    // Center the camera orbit on the black hole at (0, 0, 0)
    glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f); // Always look at the black hole center
    float radius = 1.2e11f;
    float minRadius = 1e10f, maxRadius = 1e12f;

    float azimuth = 0.0f;
    float elevation = 1.5808f;

    float orbitSpeed = 0.01f;
    float panSpeed = 0.01f;
    double zoomSpeed = 5e9f;

    // Mouse operation
    bool dragging = false;
    bool panning = false;
    bool scrolling = false;
    bool moving = false; // For compute shader optimization

    double lastX = 0.0, lastY = 0.0;

    // Calculate camera position in world space
    glm::vec3 position() const
    {
        float clampedElevation = glm::clamp(elevation, 0.01f, kPi - 0.01f);
        // Orbit around (0,0,0) always
        return glm::vec3(
            radius * sin(clampedElevation) * cos(azimuth),
            radius * cos(clampedElevation),
            radius * sin(clampedElevation) * sin(azimuth));
    }

    void update()
    {
        // Always keep target at black hole center
        target = glm::vec3(0.0f, 0.0f, 0.0f);
        moving = dragging || panning || scrolling;
        scrolling = false;
        // std::cout << elevation << std::endl;
    }

    void processMouseMove(double x, double y)
    {
        float dx = float(x - lastX);
        float dy = float(y - lastY);

        if (dragging && panning)
        {
            // Pan: Shift + Left or Middle Mouse
            // Disable panning to keep camera centered on black hole
        }
        else if (dragging && !panning)
        {
            // Orbit: Left mouse only
            azimuth += dx * orbitSpeed;
            elevation -= dy * orbitSpeed;
            elevation = glm::clamp(elevation, 0.01f, kPi - 0.01f);
        }

        lastX = x;
        lastY = y;
        update();
    }
    void processMouseButton(int button, int action, int mods, GLFWwindow *win)
    {
        if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_MIDDLE)
        {
            if (action == GLFW_PRESS)
            {
                dragging = true;
                // Disable panning so camera always orbits center
                panning = false;
                glfwGetCursorPos(win, &lastX, &lastY);
            }
            else if (action == GLFW_RELEASE)
            {
                dragging = false;
                panning = false;
            }
        }
        if (button == GLFW_MOUSE_BUTTON_RIGHT)
        {
            if (action == GLFW_PRESS)
            {
                Gravity = true;
            }
            else if (action == GLFW_RELEASE)
            {
                Gravity = false;
            }
        }
    }

    void processScroll(double xoffset, double yoffset)
    {
        radius -= yoffset * zoomSpeed;
        radius = glm::clamp(radius, minRadius, maxRadius);
        scrolling = true;
        update();
    }

    void processKey(int key, int scancode, int action, int mods)
    {
        if (action == GLFW_PRESS && key == GLFW_KEY_G)
        {
            Gravity = !Gravity;
            std::cout << "[INFO] Gravity turned " << (Gravity ? "ON" : "OFF") << std::endl;
        }
    }
};

class BlackHole
{
public:
    BlackHole(const glm::vec3 &pos, double m) : position(pos), mass(m), radius(0.0) { r_s = 2.0 * G * mass / (c * c); };
    BlackHole() = default;

    glm::vec3 position;
    double mass;
    double radius;
    double r_s; // Schwarzschild radius
};
BlackHole BH(glm::vec3(0.0f, 0.0f, 0.0f), 8.54e36); // A black hole instance

struct ObjectData
{
    glm::vec4 posRadius; // xyz = position, w = radius
    glm::vec4 color;     // rgb = color, a = unused
    float mass;
    glm::vec3 velocity = glm::vec3(0.0f, 0.0f, 0.0f); // Initial velocity
};

std::vector<ObjectData> objects = {
    {glm::vec4(4e11f, 0.0f, 0.0f, 4e10f), glm::vec4(1, 0.5, 0, 1), 1.98892e30},
    {glm::vec4(0.0f, 0.0f, 0.0f, BH.r_s), glm::vec4(0, 0, 0, 1), static_cast<float>(BH.mass)},

};
// void mouseCallback(GLFWwindow *window, double x, double y)
// {
//     static float lastX = 400.0f;
//     static float lastY = 300.0f;
//     static float yaw = 0.0f;
//     static float pitch = 0.0f;
//     static float firstMouse = true;

//     mouseX = (float)x;
//     mouseY = (float)y;
// }

class Engine
{
public:
    GLFWwindow *window;
    GLuint quadVAO;
    GLuint texture; // the actual image to write onto
    GLuint skyboxTex;
    GLuint shaderProgram;
    GLuint computeProgram = 0;

    // -- UBOs -- //
    GLuint cameraUBO = 0;
    GLuint diskUBO = 0;
    GLuint objectsUBO = 0;

    int WIDTH = 1920;
    int HEIGHT = 1080;

    /// Creates an empty engine; GL resources are allocated later via init().
    Engine() = default;
    /// Releases resources on destruction by delegating to shutdown().
    ~Engine()
    {
        shutdown();
    }

    /// Initializes windowing, GL context, shaders, UBOs, and fullscreen quad resources.
    bool init()
    {
        if (!glfwInit())
        {
            std::cerr << "GLFW init failed." << std::endl;
            exit(EXIT_FAILURE);
        }
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        window = glfwCreateWindow(WIDTH, HEIGHT, "Black Hole", nullptr, nullptr);
        if (!window)
        {
            std::cerr << "Failed to create GLFW window." << std::endl;
            glfwTerminate();
            exit(EXIT_FAILURE);
        }
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1); // Enable vsync

        glewExperimental = GL_TRUE;
        GLenum err = glewInit();
        if (err != GLEW_OK)
        {
            std::cerr << "Failed to initialize GLEW: "
                      << (const char *)glewGetErrorString(err)
                      << "\n";
            glfwTerminate();
            exit(EXIT_FAILURE);
        }
        std::cout << "OpenGL " << glGetString(GL_VERSION) << std::endl;

        this->shaderProgram = CreateShaderProgram();

        // Load skybox
        auto cubemapPath = GetExecutableDir() + "/assets/skybox";
        skyboxTex = LoadCubemap(cubemapPath);

        auto compPath = GetExecutableDir() + "/shader/geodesic.comp";
        this->computeProgram = CreateComputeProgram(compPath);

        glGenBuffers(1, &cameraUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO);
        glBufferData(GL_UNIFORM_BUFFER, 128, nullptr, GL_DYNAMIC_DRAW); // alloc ~128 bytes
        glBindBufferBase(GL_UNIFORM_BUFFER, 1, cameraUBO);

        glGenBuffers(1, &diskUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, diskUBO);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 4, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 2, diskUBO);

        glGenBuffers(1, &objectsUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, objectsUBO);
        // allocate space for 16 objects:
        // sizeof(int) + padding + 16×(vec4 posRadius + vec4 color)
        GLsizeiptr objUBOSize = sizeof(int) + 3 * sizeof(float) +
                                16 * (sizeof(glm::vec4) + sizeof(glm::vec4)) +
                                16 * sizeof(float); // 16 floats for mass
        glBufferData(GL_UNIFORM_BUFFER, objUBOSize, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 3, objectsUBO);

        auto result = QuadVAO();
        this->quadVAO = result[0];
        this->texture = result[1];

        return true;
    }

    /// Destroys GL buffers, programs, and the GLFW window before terminating GLFW.
    void shutdown()
    {
        if (objectsUBO)
            glDeleteBuffers(1, &objectsUBO);
        if (diskUBO)
            glDeleteBuffers(1, &diskUBO);
        if (cameraUBO)
            glDeleteBuffers(1, &cameraUBO);
        if (texture)
            glDeleteTextures(1, &texture);
        if (quadVAO)
            glDeleteVertexArrays(1, &quadVAO);
        if (computeProgram)
            glDeleteProgram(computeProgram);
        if (shaderProgram)
            glDeleteProgram(shaderProgram);
        if (window)
        {
            glfwDestroyWindow(window);
            window = nullptr;
        }
        glfwTerminate();
    }

    /// Builds and links the vertex and fragment shaders used for the fullscreen quad blit.
    GLuint CreateShaderProgram()
    {
        const char *vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;  // Changed to vec2
        layout (location = 1) in vec2 aTexCoord;
        out vec2 TexCoord;
        void main() {
            gl_Position = vec4(aPos, 0.0, 1.0);  // Explicit z=0
            TexCoord = aTexCoord;
        })";

        const char *fragmentShaderSource = R"(
        #version 330 core
        in vec2 TexCoord;
        out vec4 FragColor;
        uniform sampler2D screenTexture;
        void main() {
            FragColor = texture(screenTexture, TexCoord);
        })";

        // vertex shader
        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
        glCompileShader(vertexShader);

        // fragment shader
        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
        glCompileShader(fragmentShader);

        GLuint program = glCreateProgram();
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        return program;
    }

    /// Loads the compute shader from disk, compiles it, and links a compute program or exits on error.
    GLuint CreateComputeProgram(const std::string &filePath)
    {
        // 1. Read GLSL file
        std::string src = ReadTextFile(filePath);
        const char *source = src.c_str();

        // 2. Compile
        GLuint computeShader = glCreateShader(GL_COMPUTE_SHADER);
        glShaderSource(computeShader, 1, &source, nullptr);
        glCompileShader(computeShader);

        // Check compile status
        GLint ok;
        glGetShaderiv(computeShader, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            GLint logLen;
            glGetShaderiv(computeShader, GL_INFO_LOG_LENGTH, &logLen);
            std::vector<char> log(logLen);
            glGetShaderInfoLog(computeShader, logLen, nullptr, log.data());
            std::cerr << "Compute shader compile error:\n"
                      << log.data() << std::endl;
            exit(EXIT_FAILURE);
        }

        // 3. Link
        GLuint cp = glCreateProgram();
        glAttachShader(cp, computeShader);
        glLinkProgram(cp);

        // Check compile status
        glGetProgramiv(cp, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            GLint logLen;
            glGetProgramiv(cp, GL_INFO_LOG_LENGTH, &logLen);
            std::vector<char> log(logLen);
            glGetProgramInfoLog(cp, logLen, nullptr, log.data());
            std::cerr << "Compute shader link error:\n"
                      << log.data() << "\n";
            exit(EXIT_FAILURE);
        }

        glDeleteShader(computeShader);
        return cp;
    }

    void uploadCameraUBO(const Camera &cam)
    {
        // 16B align
        struct UBOData
        {
            // camera position (world space). The ORIGIN of the ray
            glm::vec3 pos;
            float _pad0;
            glm::vec3 right;
            float _pad1; // right vector
            glm::vec3 up;
            float _pad2; // up vector
            glm::vec3 forward;
            float _pad3; // forward vector, view direction
            float tanHalfFov;
            float aspect;
            bool moving;
            int _pad4;
        } data;

        // Determine camera pose in the world space
        glm::vec3 fwd = glm::normalize(cam.target - cam.position());
        glm::vec3 up = glm::vec3(0, 1, 0); // y axis is up, so disk is in x-z plane
        glm::vec3 right = normalize(cross(fwd, up));
        up = glm::cross(right, fwd);

        data.pos = cam.position();
        data.right = right;
        data.up = up;
        data.forward = fwd;
        data.tanHalfFov = tan(glm::radians(60.0f * 0.5f));
        data.aspect = float(WIDTH) / float(HEIGHT);
        data.moving = cam.dragging || cam.panning;

        glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(UBOData), &data); // pass UBOData to the shader
    }

    void uploadDiskUBO()
    {
        // disk
        float r1 = BH.r_s * 3.0f;        // inner radius of the disk
        float r2 = BH.r_s * 10.0f;       // wider disk to expose lensed outer rings
        float num = 2.0;                 // number of rays
        float thickness = BH.r_s * 0.2f; // thicker disk for a broader lensed band
        float diskData[4] = {r1, r2, num, thickness};

        glBindBuffer(GL_UNIFORM_BUFFER, diskUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(diskData), diskData);
    }

    void uploadObjectsUBO(const std::vector<ObjectData> &objs)
    {
        // 16B align
        struct UBOData
        {
            int numObjects;
            float objPad0, objPad1, objPad2; // <-- pad out to 16 bytes
            glm::vec4 posRadius[16];
            glm::vec4 color[16];
            float mass[16];
        } data;

        // Maxium objects: 16
        size_t count = std::min(objs.size(), size_t(16));
        data.numObjects = static_cast<int>(count);

        for (size_t i = 0; i < count; ++i)
        {
            data.posRadius[i] = objs[i].posRadius;
            data.color[i] = objs[i].color;
            data.mass[i] = objs[i].mass;
        }

        glBindBuffer(GL_UNIFORM_BUFFER, objectsUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(data), &data);
    }

    /// Creates the fullscreen quad geometry and an RGBA texture sized to the viewport.
    std::vector<GLuint> QuadVAO()
    {
        float quadVertices[] = {
            // positions   // texCoords
            -1.0f, 1.0f, 0.0f, 1.0f,  // top left
            -1.0f, -1.0f, 0.0f, 0.0f, // bottom left
            1.0f, -1.0f, 1.0f, 0.0f,  // bottom right

            -1.0f, 1.0f, 0.0f, 1.0f, // top left
            1.0f, -1.0f, 1.0f, 0.0f, // bottom right
            1.0f, 1.0f, 1.0f, 1.0f   // top right
        };

        GLuint VAO, VBO;
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);

        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);

        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        // Texture
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        std::vector<unsigned char> pixels(WIDTH * HEIGHT * 4, 0); // 全黑
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        std::vector<GLuint> VAOtexture = {VAO, texture};

        return VAOtexture;
    }
    /// Draws the fullscreen quad using the current texture bound to the shader.
    void drawFullScreenQuad()
    {
        glEnable(GL_FRAMEBUFFER_SRGB);
        glUseProgram(shaderProgram);
        glBindVertexArray(quadVAO);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(glGetUniformLocation(shaderProgram, "screenTexture"), 0);

        glDisable(GL_DEPTH_TEST);         // draw as background
        glDrawArrays(GL_TRIANGLES, 0, 6); // 2 triangles
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_FRAMEBUFFER_SRGB);
    }

    /// Runs the compute shader to fill the render texture sized to the current viewport.
    void dispatchCompute(const Camera &cam)
    {
        // determine target compute‐res
        int cw = cam.moving ? WIDTH / 3 : WIDTH;
        int ch = cam.moving ? HEIGHT / 3 : HEIGHT;

        // 1. reallocate the texture if needed (given pixel changed dynamically)
        static int lastW = 0, lastH = 0;
        if (cw != lastW || ch != lastH)
        {
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, cw, ch, 0, GL_RGBA, GL_FLOAT, nullptr);
            lastW = cw;
            lastH = ch;
        }
        // 2. Bind UBOs
        glUseProgram(computeProgram);
        uploadCameraUBO(cam);
        uploadDiskUBO();
        uploadObjectsUBO(objects);

        // 3. Bind texture as image0
        glBindImageTexture(0, texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

        // 3.1 Bind skybox cubemap
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxTex);
        glUniform1i(glGetUniformLocation(computeProgram, "skybox"), 0);

        // 4. Dispatch grid (work groups)
        GLuint groupsX = (GLuint)std::ceil(cw / 16.0f);
        GLuint groupsY = (GLuint)std::ceil(ch / 16.0f);
        glDispatchCompute(groupsX, groupsY, 1);

        // 5. Sync
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }

    // void drawGrid(const glm::mat4 &viewProj);
    // void generateGrid(const std::vector<ObjectData> &objects);
    GLuint LoadCubemap(const std::string &cubemapDir)
    {
        const std::vector<std::string> faces = {"right", "left", "top", "bottom", "front", "back"};
        GLuint cubemapID;
        glGenTextures(1, &cubemapID);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapID);

        int w, h, channels;
        stbi_set_flip_vertically_on_load(false);
        for (GLuint i = 0; i < faces.size(); i++)
        {
            const std::string facePath = cubemapDir + "/" + faces[i] + ".png";
            unsigned char *data = stbi_load(facePath.c_str(), &w, &h, &channels, 0);
            if (data)
            {
                GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;
                GLenum internalFormat = (channels == 4) ? GL_SRGB8_ALPHA8 : GL_SRGB8;
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, internalFormat, w,
                             h, 0, format, GL_UNSIGNED_BYTE, data);
                stbi_image_free(data);
            }
            else
            {
                std::cout << "Cubemap texture failed to load at path: "
                          << facePath << std::endl;
                stbi_image_free(data);
            }
        }

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        return cubemapID;
    }
};

void setupCameraCallbacks(GLFWwindow *window, Camera *camera)
{
    glfwSetWindowUserPointer(window, camera);

    glfwSetMouseButtonCallback(window, [](GLFWwindow *win, int button, int action, int mods)
                               {
        Camera* cam = (Camera*)glfwGetWindowUserPointer(win);
        cam->processMouseButton(button, action, mods, win); });

    glfwSetCursorPosCallback(window, [](GLFWwindow *win, double x, double y)
                             {
        Camera* cam = (Camera*)glfwGetWindowUserPointer(win);
        cam->processMouseMove(x, y); });

    glfwSetScrollCallback(window, [](GLFWwindow *win, double xoffset, double yoffset)
                          {
        Camera* cam = (Camera*)glfwGetWindowUserPointer(win);
        cam->processScroll(xoffset, yoffset); });

    glfwSetKeyCallback(window, [](GLFWwindow *win, int key, int scancode, int action, int mods)
                       {
        Camera* cam = (Camera*)glfwGetWindowUserPointer(win);
        cam->processKey(key, scancode, action, mods); });
}

int main(int, char **)
{
    Camera camera;
    Engine engine;
    if (!engine.init())
    {
        return 1;
    }

    setupCameraCallbacks(engine.window, &camera);

    glViewport(0, 0, engine.WIDTH, engine.HEIGHT);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    while (!glfwWindowShouldClose(engine.window))
    {
        camera.update();
        glViewport(0, 0, engine.WIDTH, engine.HEIGHT);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        engine.dispatchCompute(camera);
        engine.drawFullScreenQuad();

        // ... 重力更新、dispatchCompute、drawFullScreenQuad ...
        glfwSwapBuffers(engine.window);
        glfwPollEvents();
    }
    return 0; // engine 析构时自动 shutdown
}
