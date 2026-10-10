#include "render/core/shader_program.h"

#include "core/log.h"

#include <EGL/egl.h>
#include <GLES2/gl2ext.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

  constexpr Logger kLog("shader");

  // kusanagi: linked programs are kept on disk as program binaries. Compiling any GLSL makes Mesa build
  // its whole built-in function library, about 7 MB that stays for the life of the process; a shell whose
  // programs all load from binaries never pays it. A binary the driver rejects (driver update, damaged
  // file) is compiled from source again and replaced.
  constexpr std::uint32_t kBinaryMagic = 0x4250534BU; // "KSPB"

  std::uint64_t fnv1a64(std::uint64_t hash, std::string_view text) {
    for (const char ch : text) {
      hash ^= static_cast<unsigned char>(ch);
      hash *= 1099511628211ULL;
    }
    return hash;
  }

  constexpr std::uint64_t kFnvBasis = 14695981039346656037ULL;

  struct ProgramBinaryApi {
    PFNGLGETPROGRAMBINARYOESPROC get = nullptr;
    PFNGLPROGRAMBINARYOESPROC load = nullptr;
    // Renderer and version: file names start with it, so binaries of an older driver can be told apart.
    std::string driverPrefix;
  };

  const ProgramBinaryApi& programBinaryApi() {
    static const ProgramBinaryApi api = [] {
      ProgramBinaryApi out;
      const auto* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
      if (extensions == nullptr || std::strstr(extensions, "GL_OES_get_program_binary") == nullptr) {
        return out;
      }
      GLint formats = 0;
      glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS_OES, &formats);
      if (formats <= 0) {
        return out;
      }
      out.get = reinterpret_cast<PFNGLGETPROGRAMBINARYOESPROC>(eglGetProcAddress("glGetProgramBinaryOES"));
      out.load = reinterpret_cast<PFNGLPROGRAMBINARYOESPROC>(eglGetProcAddress("glProgramBinaryOES"));
      const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
      const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
      const std::uint64_t driver = fnv1a64(
          fnv1a64(kFnvBasis, renderer != nullptr ? renderer : ""), version != nullptr ? version : ""
      );
      out.driverPrefix = std::format("{:08x}-", static_cast<std::uint32_t>(driver ^ (driver >> 32)));
      return out;
    }();
    return api;
  }

  std::filesystem::path programCacheDir() {
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && xdg[0] != '\0') {
      return std::filesystem::path(xdg) / "kusanagi" / "shaders";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
      return std::filesystem::path(home) / ".cache" / "kusanagi" / "shaders";
    }
    return {};
  }

  std::filesystem::path programCachePath(const char* vertexSource, const char* fragmentSource) {
    const auto& api = programBinaryApi();
    if (api.get == nullptr || api.load == nullptr) {
      return {};
    }
    const auto dir = programCacheDir();
    if (dir.empty()) {
      return {};
    }
    const std::uint64_t hash = fnv1a64(fnv1a64(fnv1a64(kFnvBasis, vertexSource), "\x1f"), fragmentSource);
    return dir / std::format("{}{:016x}.bin", api.driverPrefix, hash);
  }

  void drainGlErrors() {
    for (int i = 0; i < 8 && glGetError() != GL_NO_ERROR; ++i) {
    }
  }

  // Returns a linked program, or 0 when there is no usable binary.
  GLuint loadProgramBinary(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
      return 0;
    }
    std::uint32_t magic = 0;
    std::uint32_t format = 0;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&format), sizeof(format));
    if (!file || magic != kBinaryMagic) {
      return 0;
    }
    const std::vector<char> data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (data.empty()) {
      return 0;
    }
    const GLuint program = glCreateProgram();
    if (program == 0) {
      return 0;
    }
    programBinaryApi().load(program, static_cast<GLenum>(format), data.data(), static_cast<GLint>(data.size()));
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
      glDeleteProgram(program);
      drainGlErrors();
      kLog.debug("program binary {} rejected; compiling from source", path.filename().string());
      return 0;
    }
    return program;
  }

  void saveProgramBinary(GLuint program, const std::filesystem::path& path) {
    GLint length = 0;
    glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH_OES, &length);
    if (length <= 0) {
      return;
    }
    std::vector<char> data(static_cast<std::size_t>(length));
    GLenum format = 0;
    GLsizei written = 0;
    programBinaryApi().get(program, length, &written, &format, data.data());
    if (written <= 0) {
      drainGlErrors();
      return;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    auto tmp = path;
    tmp += ".tmp";
    {
      std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
      if (!file) {
        return;
      }
      const std::uint32_t magic = kBinaryMagic;
      const auto format32 = static_cast<std::uint32_t>(format);
      file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
      file.write(reinterpret_cast<const char*>(&format32), sizeof(format32));
      file.write(data.data(), written);
      if (!file) {
        file.close();
        std::filesystem::remove(tmp, ec);
        return;
      }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
      std::filesystem::remove(tmp, ec);
      return;
    }

    // A new binary is only written after a cache miss, which is also when a driver update shows up:
    // drop the binaries of other drivers then.
    const std::string& prefix = programBinaryApi().driverPrefix;
    for (std::filesystem::directory_iterator it(path.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
      const std::string name = it->path().filename().string();
      if (name.ends_with(".bin") && !name.starts_with(prefix)) {
        std::error_code removeError;
        std::filesystem::remove(it->path(), removeError);
      }
    }
  }

  GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    if (shader == 0) {
      throw std::runtime_error("glCreateShader failed");
    }

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      GLint logLength = 0;
      glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
      std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
      glGetShaderInfoLog(shader, logLength, nullptr, log.data());
      glDeleteShader(shader);
      throw std::runtime_error("shader compilation failed: " + log);
    }

    return shader;
  }

} // namespace

ShaderProgram::~ShaderProgram() { destroy(); }

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept : m_program(other.m_program) { other.m_program = 0; }

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  destroy();
  m_program = other.m_program;
  other.m_program = 0;
  return *this;
}

void ShaderProgram::create(const char* vertexSource, const char* fragmentSource) {
  destroy();

  const auto cachePath = programCachePath(vertexSource, fragmentSource);
  if (!cachePath.empty()) {
    m_program = loadProgramBinary(cachePath);
    if (m_program != 0) {
      return;
    }
  }

  const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
  const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);

  m_program = glCreateProgram();
  if (m_program == 0) {
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    throw std::runtime_error("glCreateProgram failed");
  }

  glAttachShader(m_program, vertexShader);
  glAttachShader(m_program, fragmentShader);
  glLinkProgram(m_program);

  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  GLint linked = 0;
  glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
  if (linked == GL_FALSE) {
    GLint logLength = 0;
    glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
    glGetProgramInfoLog(m_program, logLength, nullptr, log.data());
    destroy();
    throw std::runtime_error("shader link failed: " + log);
  }

  if (!cachePath.empty()) {
    saveProgramBinary(m_program, cachePath);
  }
}

void ShaderProgram::destroy() {
  if (m_program != 0) {
    glDeleteProgram(m_program);
    m_program = 0;
  }
}

void ShaderProgram::abandon() noexcept { m_program = 0; }

bool ShaderProgram::isValid() const noexcept { return m_program != 0; }

GLuint ShaderProgram::id() const noexcept { return m_program; }
