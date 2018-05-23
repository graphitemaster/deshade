#include <mutex>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <fstream>
#include <streambuf>

#include <cstring> // std::memcpy, std::strlen

extern "C"
{
	#include <dlfcn.h>
}

#include <GL/glx.h>

typedef Bool (*GLXMAINPROC)(uint32_t, const void*, void*, void*); // glvnd
typedef void (*(*GLXGETPROCADDRESSPROC)(const GLubyte*))(); // glx

typedef GLuint (*GLCREATESHADERPROC)(GLenum); // gl
typedef void (*GLDELETESHADERPROC)(GLuint); // gl
typedef void (*GLSHADERSOURCEPROC)(GLuint, GLsizei, const GLchar**, const GLint*); // gl

extern "C" void * __libc_dlopen_mode(const char* filename, int flag);
extern "C" void * __libc_dlsym(void* handle, const char* symbol);

// 128 bit hash via djbx33ax4 (Daniel Bernstein Times 33 with Addition interleaved 4x for 128 bits)
static void hash128(const unsigned char *buffer, size_t size, unsigned char *out)
{
	const unsigned char *const end = (const unsigned char *const )buffer + size;
	uint32_t state[] = { 5381, 5381, 5381, 5381 };
	size_t s = 0;
	for (const unsigned char *p = buffer; p < end; p++)
	{
		state[s] = state[s] * 33  + *p;
		s = (s+1) & 0x03;
	}
	std::memcpy(out, state, sizeof state);
}

static std::string hash128(const unsigned char *buffer, size_t size)
{
	std::string result;
	unsigned char output[16];
	hash128(buffer, size, output);
	static const char *k_hex = "0123456789ABCDEF";
	for (size_t i = 0; i < sizeof output; ++i)
	{
		result += k_hex[(output[i] >> 4) & 0x0F];
		result += k_hex[output[i] & 0x0F];
	}
	return result;
}

struct Context
{
	Context();

	// dynamic linker functions are replaced, these are the original
	// needed to forward from the replacement
	void* (*dlsym_)(void*, const char*);
	void* (*dlopen_)(const char*, int);
	int (*dlclose_)(void*);

	// everything below protected by mutex_
	std::recursive_mutex mutex_;
	std::unordered_map<void*, std::string> object_handle_to_name;
	std::unordered_map<GLuint, GLenum> shader_handle_to_type;
	GLXMAINPROC glx_Main_;
	GLXGETPROCADDRESSPROC glXGetProcAddress_;
	GLXGETPROCADDRESSPROC glXGetProcAddressARB_;
	GLCREATESHADERPROC glCreateShader_;
	GLDELETESHADERPROC glDeleteShader_;
	GLSHADERSOURCEPROC glShaderSource_;

	// log file
	std::ofstream log_;
};

Context::Context()
	: dlsym_                { nullptr }
	, dlopen_               { nullptr }
	, dlclose_              { nullptr }
	, glx_Main_             { nullptr }
	, glXGetProcAddress_    { nullptr }
	, glXGetProcAddressARB_ { nullptr }
	, glCreateShader_       { nullptr }
	, glDeleteShader_       { nullptr }
	, glShaderSource_       { nullptr }
	, log_                  { "deshade.txt" }
{
	void* libdl = __libc_dlopen_mode("libdl.so.2", RTLD_LOCAL | RTLD_NOW);
	if (libdl)
	{
		*(void **)&dlsym_   = __libc_dlsym(libdl, "dlsym");
		*(void **)&dlopen_  = __libc_dlsym(libdl, "dlopen");
		*(void **)&dlclose_ = __libc_dlsym(libdl, "dlclose");
		dlclose_(libdl);
	}
}

static Context& GetContext()
{
	// context has to leak on exit, _dl_fini will want to call our dlclose
	// which depends on the context existing
	static Context* context_ = nullptr;
	static std::once_flag once;
	std::call_once(once, [](){ context_ = new Context; });
	return *context_;
}

void Log(const char *string)
{
	Context& context = GetContext();
	while (*string)
	{
		if (*string == '%')
		{
			if (*(string + 1) == '%')
			{
				++string;
			}
			else
			{
				abort();
			}
		}
		context.log_ << *string++;
	}
	context.log_.flush();
}

template<typename T, typename... Ts>
void Log(const char *string, T value, Ts&&... args)
{
	Context& context = GetContext();
	while (*string)
	{
		if (*string == '%')
		{
			if (*(string + 1) == '%')
			{
				++string;
			}
			else
			{
				context.log_ << value;
				Log(string + 1, std::forward<Ts>(args)...);
				return;
			}
		}
		context.log_ << *string++;
	}
}

static const char* GetShaderExtensionString(GLenum shader_type)
{
	switch (shader_type)
	{
	case GL_VERTEX_SHADER:
		return "_vs.glsl";
	case GL_FRAGMENT_SHADER:
		return "_fs.glsl";
	case GL_COMPUTE_SHADER:
		return "_cs.glsl";
	case GL_GEOMETRY_SHADER:
		return "_gs.glsl";
	case GL_TESS_CONTROL_SHADER:
		return "_tsc.glsl";
	case GL_TESS_EVALUATION_SHADER:
		return "_tse.glsl";
	}
	return "<unknown>";
}

static const char* GetShaderTypeString(GLenum shader_type)
{
	switch (shader_type)
	{
	case GL_VERTEX_SHADER:
		return "vertex";
	case GL_FRAGMENT_SHADER:
		return "fragment";
	case GL_COMPUTE_SHADER:
		return "compute";
	case GL_GEOMETRY_SHADER:
		return "geometry";
	case GL_TESS_CONTROL_SHADER:
		return "tesselation control";
	case GL_TESS_EVALUATION_SHADER:
		return "tesselation evaluation";
	}
	return "<unknown>";
}

// Replacement OpenGL shader functions
static GLuint CreateShader(GLenum shader_type)
{
	Context& context = GetContext();
	std::lock_guard<std::recursive_mutex> lock(context.mutex_);
	GLuint result = context.glCreateShader_(shader_type);
	if (result)
	{
		Log("Created % shader \"%\"\n", GetShaderTypeString(shader_type), result);
		context.shader_handle_to_type.insert({ result, shader_type });
		return result;
	}
	return 0;
}

static void DeleteShader(GLuint shader)
{
	Context& context = GetContext();
	std::lock_guard<std::recursive_mutex> lock(context.mutex_);
	auto find = context.shader_handle_to_type.find(shader);
	if (find != context.shader_handle_to_type.end())
	{
		Log("Deleted % shader \"%\"\n", GetShaderTypeString(find->second), shader);
		context.shader_handle_to_type.erase(find);
	}
	context.glDeleteShader_(shader);
}

static void ShaderSource(GLuint shader, GLsizei count, const GLchar** string, const GLint* length)
{
	Context& context = GetContext();
	std::lock_guard<std::recursive_mutex> lock(context.mutex_);

	GLenum shader_type = 0;
	auto find = context.shader_handle_to_type.find(shader);
	if (find != context.shader_handle_to_type.end())
	{
		shader_type = find->second;
	}
	const char* shader_type_string = GetShaderTypeString(shader_type);

	// concatenate all shader source by default, we only ever call glShaderSource with count = 1
	std::vector<char> source;
	if (length) for (GLsizei i = 0; i < count; i++)
	{
		source.insert(source.end(), string[i], string[i] + length[i]);
	}
	else for (GLsizei i = 0; i < count; i++)
	{
		source.insert(source.end(), string[i], string[i] + std::strlen(string[i]));
	}

	// replace all \r in the text
	source.erase(std::remove_if(source.begin(), source.end(), [](char ch) { return ch == '\r'; }), source.end());

	// calculate hash
	std::string hash = hash128((const unsigned char *)source.data(), source.size());

	// construct string from contents
	std::string contents;

	// check if a shader replacement exists
	std::string file_name = "shaders/" + hash + GetShaderExtensionString(shader_type);
	std::ifstream file_contents(file_name);
	if (file_contents.is_open())
	{
		// construct string from replacement contents
		Log("Replaced % shader \"%\"\n", shader_type_string, hash);
		contents.assign((std::istreambuf_iterator<char>(file_contents)),
		                 std::istreambuf_iterator<char>());
	}
	else
	{
		// construct string from source contents
		contents.assign(source.begin(), source.end());

		// write the contents to a file
		std::ofstream file(file_name);
		if (file.is_open())
		{
			file << contents;
			Log("Dumpped % shader \"%\"\n", shader_type_string, hash);
		}
	}

	// place the actual call
	const GLchar* shader_data = (const GLchar*)contents.c_str();
	const GLint shader_size = contents.size();
	context.glShaderSource_(shader, 1, &shader_data, &shader_size);
	Log("Source % shader \"%\"\n", shader_type_string, hash);
}

static bool Match(const std::string& name, const char *match)
{
	if (name         == match) return true;
	if (name + "ARB" == match) return true;
	if (name + "EXT" == match) return true;
	return false;
}

static void* ApplyReplacements(const char* name, void* handle)
{
	Context& context = GetContext();
	if (Match("glCreateShader", name))
	{
		*(void **)&context.glCreateShader_ = handle;
		return (void *)&CreateShader;
	}
	if (Match("glDeleteShader", name))
	{
		*(void **)&context.glDeleteShader_ = handle;
		return (void *)&DeleteShader;
	}
	if (Match("glShaderSource", name))
	{
		*(void **)&context.glShaderSource_ = handle;
		return (void *)&ShaderSource;
	}
	return nullptr;
}

static void* GetProcAddress(const GLubyte* symbol)
{
	const char *name = (const char *)symbol;
	Context& context = GetContext();
	std::lock_guard<std::recursive_mutex> lock(context.mutex_);
	void *result = (void *)context.glXGetProcAddress_(symbol);
	void *replace = ApplyReplacements(name, result);
	if (replace)
	{
		Log("Intercepted: \"%\" % /* replaced with % */\n", name, result, replace);
		result = replace;
	}
	return result;
}

static void* GetProcAddressARB(const GLubyte* symbol)
{
	const char *name = (const char *)symbol;
	Context& context = GetContext();
	std::lock_guard<std::recursive_mutex> lock(context.mutex_);
	void *result = (void *)context.glXGetProcAddressARB_(symbol);
	void *replace = ApplyReplacements(name, result);
	if (replace)
	{
		Log("Intercepted: \"%\" % /* replaced with % */\n", name, result, replace);
		result = replace;
	}
	return result;
}

// replace __glx_Main as an export
extern "C" Bool __glx_Main(uint32_t version, const void *exports, void *vendor, void *imports)
{
	Context& context = GetContext();
	std::lock_guard<std::recursive_mutex> lock(context.mutex_);

	Bool result = context.glx_Main_(version, exports, vendor, imports);

	// __glx_Main import table is not worth changing, we can just fetch the new ones
	// after we enter here because this will be called from inside libGLX_{vendor}.so only
	*(void **)&context.glXGetProcAddress_    = context.dlsym_(RTLD_NEXT, "glXGetProcAddress");
	*(void **)&context.glXGetProcAddressARB_ = context.dlsym_(RTLD_NEXT, "glXGetProcAddressARB");

	Log("Intercepted: \"glXGetProcAddress\" % /* replaced with % */\n",
		*(void **)&context.glXGetProcAddress_, (void *)&GetProcAddress);

	Log("Intercepted: \"glXGetProcAddressARB\" % /* replaced with % */\n",
		*(void **)&context.glXGetProcAddressARB_, (void *)&GetProcAddressARB);

	return result;
}

// replace loader incase the application dlopen's and fetches GL functions this way
extern "C" void* dlsym(void* handle, const char* symbol)
{
	Context& context = GetContext();
	std::string name = "<unknown>";
	context.mutex_.lock();
	auto find = context.object_handle_to_name.find(handle);
	if (find != context.object_handle_to_name.end())
	{
		name = find->second;
	}
	context.mutex_.unlock();
	void *result = context.dlsym_(handle, symbol);
	bool valid_name = true;
	if (handle == RTLD_NEXT || handle == RTLD_DEFAULT)
	{
		if (handle == RTLD_DEFAULT)
		{
			name = "RTLD_DEFAULT";
		}
		else if (handle == RTLD_NEXT)
		{
			name = "RTLD_NEXT";
		}

		Log("Forwarding: dlsym(%, \"%\") = %\n", name, symbol, result);

		valid_name = false;
	}

	if (!strcmp(symbol, "__glx_Main"))
	{
		// replace __glx_Main with our own if we're using glvnd
		std::lock_guard<std::recursive_mutex> lock(context.mutex_);
		*(void **)&context.glx_Main_ = result;
		void *replace = (void *)&__glx_Main;
		Log("Intercepted: dlsym(% /* % */, \"%\") = % /* replaced with % */\n", handle, name, symbol, result, replace);
		return replace;
	}
	else if (!strcmp(symbol, "glXGetProcAddress"))
	{
		// replace glXGetProcAddress with our wrapper
		std::lock_guard<std::recursive_mutex> lock(context.mutex_);
		*(void **)&context.glXGetProcAddress_ = result;
		void *replace = (void *)&GetProcAddress;
		Log("Intercepted: dlsym(% /* % */, \"%\") = % /* replaced with % */\n", handle, name, symbol, result, replace);
		return replace;
	}
	else if (!strcmp(symbol, "glXGetProcAddressARB"))
	{
		// replace glXGetProcAddressARB with our wrapper
		std::lock_guard<std::recursive_mutex> lock(context.mutex_);
		*(void **)&context.glXGetProcAddressARB_ = result;
		void *replace = (void *)&GetProcAddressARB;
		Log("Intercepted: dlsym(% /* % */, \"%\") = % /* replaced with % */\n", handle, name, symbol, result, replace);
		return replace;
	}

	if (valid_name)
	{
		Log("Forwarding: dlsym(% /* % */, \"%\") = %\n", handle, name, symbol, result);
	}

	return result;
}

extern "C" void* dlopen(const char* name, int flags)
{
	Context& context = GetContext();
	void *result = context.dlopen_(name, flags);
	const char *safe_name = name;
	if (name == RTLD_NEXT || name == RTLD_DEFAULT)
	{
		if (name == RTLD_DEFAULT)
		{
			safe_name = "RTLD_DEFAULT";
		}
		if (name == RTLD_NEXT)
		{
			safe_name = "RTLD_NEXT";
		}
		Log("Forwarding: dlopen(%, %) = %\n", safe_name, flags, result);
	}
	else
	{
		Log("Forwarding: dlopen(\"%\", %) = %\n", safe_name, flags, result);
	}
	if (result)
	{
		context.mutex_.lock();
		context.object_handle_to_name.insert({ result, safe_name });
		context.mutex_.unlock();
	}
	return result;
}

extern "C" int dlclose(void* handle)
{
	Context& context = GetContext();
	context.mutex_.lock();
	auto find = context.object_handle_to_name.find(handle);
	std::string name = "<unknown>";
	if (find != context.object_handle_to_name.end())
	{
		name = find->second;
		context.object_handle_to_name.erase(handle);
	}
	context.mutex_.unlock();
	int result = context.dlclose_(handle);
	Log("Forwarding: dlclose(% /* % */) = %\n", handle, name, result);
	return result;
}

static void ReplaceExport(bool ARB)
{
	Context& context = GetContext();
	context.mutex_.lock();
	if (!ARB && !context.glXGetProcAddress_)
	{
		*(void **)&context.glXGetProcAddress_ = context.dlsym_(RTLD_NEXT, "glXGetProcAddress");
		Log("Intercepted: \"glXGetProcAddress\" % /* replaced with % */ \n",
			*(void**&)context.glXGetProcAddress_, (void *)&GetProcAddress);
	}
	else if (ARB && !context.glXGetProcAddressARB_)
	{
		*(void **)&context.glXGetProcAddressARB_ = context.dlsym_(RTLD_NEXT, "glXGetProcAddressARB");
		Log("Intercepted: \"glXGetProcAddressARB\" % /* replaced with % */\n",
			*(void **)&context.glXGetProcAddressARB_, (void *)&GetProcAddressARB);
	}
	context.mutex_.unlock();
}

// replace glXGetProcAddress export with our wrapper
extern "C" void (*glXGetProcAddress(const GLubyte* symbol))()
{
	static std::once_flag once;
	std::call_once(once, [](){ReplaceExport(false);});
	return (void (*)())GetProcAddress(symbol);
}

// replace glXGetProcAddressARB export with our wrapper
extern "C" void (*glXGetProcAddressARB(const GLubyte* symbol))()
{
	static std::once_flag once;
	std::call_once(once, [](){ReplaceExport(true);});
	return (void (*)())GetProcAddressARB(symbol);
}
