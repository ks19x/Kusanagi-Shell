#pragma once

#include "render/core/texture_handle.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct LoadedImageFile;

class GlSharedContext;
class TextureManager;

// Path-keyed, refcounted texture cache backed by a TextureManager living in
// the shared EGL context. Any subsystem in the shared context's share group
// (shell, lockscreen, backdrop, wallpaper) can acquire a texture by path and
// get back the same TextureId — textures are decoded and uploaded once.
class SharedTextureCache {
public:
  SharedTextureCache() = default;
  ~SharedTextureCache();

  SharedTextureCache(const SharedTextureCache&) = delete;
  SharedTextureCache& operator=(const SharedTextureCache&) = delete;

  void initialize(GlSharedContext* sharedGl);

  [[nodiscard]] bool shared() const noexcept { return m_sharedGl != nullptr; }

  [[nodiscard]] TextureHandle acquire(const std::string& path);
  // Decodes `path` on a worker thread, then runs `ready` on the main thread, where acquire()
  // only has to upload. A 4K wallpaper takes ~200 ms to decode, too long for the main loop.
  // Decoded pixels that nothing acquires in `ready` are dropped.
  void preload(const std::string& path, std::function<void()> ready);
  // True when the texture is uploaded, or decoded by preload() and waiting for acquire(). Acquiring it is cheap then.
  [[nodiscard]] bool resident(const std::string& path) const;
  [[nodiscard]] TextureHandle acquireAlphaMask(const std::string& path);
  [[nodiscard]] TextureHandle peek(const std::string& path) const;
  [[nodiscard]] TextureHandle peekAlphaMask(const std::string& path) const;
  void release(TextureHandle& handle, const std::string& path);
  void releaseAlphaMask(TextureHandle& handle, const std::string& path);
  void abandonGpuResources() noexcept;
  void reloadResidentTextures();

private:
  // Returns false if no usable GL context could be bound (e.g. context lost on resume);
  // callers skip the upload/unload and retry on the next graphics-reset rebuild.
  [[nodiscard]] bool makeCurrent();

  struct Entry {
    TextureHandle handle;
    int refCount = 0;
  };

  GlSharedContext* m_sharedGl = nullptr;
  std::unique_ptr<TextureManager> m_textureManager;
  std::unordered_map<std::string, Entry> m_entries;
  std::unordered_map<std::string, Entry> m_alphaMaskEntries;
  std::unordered_map<std::string, std::shared_ptr<LoadedImageFile>> m_decoded;
  std::unordered_map<std::string, std::vector<std::function<void()>>> m_preloadWaiters;
  std::shared_ptr<int> m_alive = std::make_shared<int>(0);
};
