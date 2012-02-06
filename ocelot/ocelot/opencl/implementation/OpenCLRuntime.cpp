// C standard library includes
#include <assert.h>

// C++ standard library includes
#include <sstream>
#include <fstream>
#include <algorithm>

// Ocelot includes
#include <ocelot/opencl/interface/OpenCLRuntime.h>
#include <ocelot/ir/interface/PTXInstruction.h>
#include <ocelot/executive/interface/RuntimeException.h>
#include <ocelot/transforms/interface/PassManager.h>

// Hydrazine includes
#include <hydrazine/implementation/Exception.h>
#include <hydrazine/implementation/string.h>
#include <hydrazine/implementation/debug.h>

#ifdef REPORT_BASE
#undef REPORT_BASE
#endif

////////////////////////////////////////////////////////////////////////////////

// whether OPENCL runtime catches exceptions thrown by Ocelot
#define CATCH_RUNTIME_EXCEPTIONS 0

// whether verbose error messages are printed
#define OPENCL_VERBOSE 1

// whether debugging messages are printed
#define REPORT_BASE 0

// report all ptx modules
//#define REPORT_ALL_PTX 0

////////////////////////////////////////////////////////////////////////////////
//
// Error handling macros

#define Ocelot_Exception(x) { std::stringstream ss; ss << x; \
	throw hydrazine::Exception(ss.str()); }

////////////////////////////////////////////////////////////////////////////////

typedef api::OcelotConfiguration config;

/*
opencl::RegisteredTexture::RegisteredTexture(const std::string& m, 
	const std::string& t, bool n) : module(m), texture(t), norm(n) {
	
}

opencl::RegisteredGlobal::RegisteredGlobal(const std::string& m, 
	const std::string& g) : module(m), global(g) {

}

opencl::Dimension::Dimension(int _x, int _y, int _z, 
	const openclChannelFormatDesc& _f) : x(_x), y(_y), z(_z), format(_f) {

}

size_t opencl::Dimension::pitch() const {
	return ((format.x + format.y + format.z + format.w) / 8) * x;
}
*/
////////////////////////////////////////////////////////////////////////////////
void opencl::OpenCLRuntime::_enumeratePlatforms() {
	report("Create platforms ");
	Platform * p = new Platform();
	_platforms.push_back(p);

	_enumerateDevices(p);
}

opencl::Context * opencl::OpenCLRuntime::_createContext() {
	report("Creating new context ");
	Context * c = new Context();
	_contexts.push_back(c);
	return c;
}

void opencl::OpenCLRuntime::_enumerateDevices(Platform * platform) {
	if(_devicesLoaded) return;
	report("Creating devices.");
	if(config::get().executive.enableNVIDIA) {
		executive::DeviceVector d = 
			executive::Device::createDevices(ir::Instruction::SASS, _flags,
				_computeCapability);
		report(" - Added " << d.size() << " nvidia gpu devices." );

		for(size_t i = 0; i < d.size(); i++)
			_devices.push_back(new Device(d[i], CL_DEVICE_TYPE_GPU, platform));
	}
	if(config::get().executive.enableEmulated) {
		executive::DeviceVector d = 
			executive::Device::createDevices(ir::Instruction::Emulated, _flags,
				_computeCapability);
		report(" - Added " << d.size() << " emulator devices." );
		
		for(size_t i = 0; i < d.size(); i++)
			_devices.push_back(new Device(d[i], CL_DEVICE_TYPE_GPU, platform));
	}
	if(config::get().executive.enableLLVM) {
		executive::DeviceVector d = 
			executive::Device::createDevices(ir::Instruction::LLVM, _flags,
				_computeCapability);
		report(" - Added " << d.size() << " llvm-cpu devices." );
		for(size_t i = 0; i < d.size(); i++)
			_devices.push_back(new Device(d[i], CL_DEVICE_TYPE_CPU, platform));
		
		if (config::get().executive.workerThreadLimit > 0) {
			for (executive::DeviceVector::iterator d_it = d.begin();
				d_it != d.end(); ++d_it) {
				(*d_it)->limitWorkerThreads(
					config::get().executive.workerThreadLimit);
			}
		}
	}
	if(config::get().executive.enableAMD) {
		executive::DeviceVector d =
			executive::Device::createDevices(ir::Instruction::CAL, _flags,
				_computeCapability);
		report(" - Added " << d.size() << " amd gpu devices." );
		for(size_t i = 0; i < d.size(); i++)
			_devices.push_back(new Device(d[i], CL_DEVICE_TYPE_GPU, platform));
	}
	if(config::get().executive.enableRemote) {
		executive::DeviceVector d =
			executive::Device::createDevices(ir::Instruction::Remote, _flags,
				_computeCapability);
		report(" - Added " << d.size() << " remote devices." );
		for(size_t i = 0; i < d.size(); i++)
			_devices.push_back(new Device(d[i], CL_DEVICE_TYPE_GPU, platform));
	}
	
	_devicesLoaded = true;
	
	if(_devices.empty())
	{
		std::cerr << "==Ocelot== WARNING - No OpenCL devices found or all " 
			<< "devices disabled!\n";
		std::cerr << "==Ocelot==  Consider enabling the emulator in " 
			<< "configure.ocelot.\n";
	}

	
}

//! acquires mutex and locks the runtime
void opencl::OpenCLRuntime::_lock() {
	_mutex.lock();
}

//! releases mutex
void opencl::OpenCLRuntime::_unlock() {
	_mutex.unlock();
}

/*
opencl::Context& opencl::OpenCLRuntime::_bind() {
	_enumerateDevices();

	Context& thread = _createContext();

	if (_devices.empty()) return thread;
	
	_selectedDevice = thread.selectedDevice;
	executive::Device& device = _getDevice();

	assert(!device.selected());
	device.select();
	
	return thread;
}

void opencl::OpenCLRuntime::_unbind() {
	executive::Device& device = _getDevice();
	assert(_createContext().selectedDevice == _selectedDevice);
	
	_selectedDevice = NULL;
	assert(device.selected());
	device.unselect();
}

void opencl::OpenCLRuntime::_acquire() {
	_lock();
	_bind();
	if (_devices.empty()) _unlock();
}

void opencl::OpenCLRuntime::_release() {
	_unbind();
	_unlock();
}
*/

/*
executive::Device& opencl::OpenCLRuntime::_getDevice() {
	return *((executive::Device *)_selectedDevice);
}
*/
std::string opencl::OpenCLRuntime::_formatError( const std::string& message ) {
	std::string result = "==Ocelot== ";
	for(std::string::const_iterator mi = message.begin(); 
		mi != message.end(); ++mi) {
		result.push_back(*mi);
		if(*mi == '\n') {
			result.append("==Ocelot== ");
		}
	}
	return result;
}


void opencl::OpenCLRuntime::_registerModule(ModuleMap::iterator module, executive::Device * device) {
	if(module->second.loaded()) return;
	module->second.loadNow();	
/*	for(RegisteredTextureMap::iterator texture = _textures.begin(); 
		texture != _textures.end(); ++texture) {
		if(texture->second.module != module->first) continue;
		ir::Texture* tex = module->second.getTexture(texture->second.texture);
		assert(tex != 0);
		tex->normalizedFloat = texture->second.norm;
	}
	
	transforms::PassManager manager(&module->second);
	
	for(PassSet::iterator pass = _passes.begin(); pass != _passes.end(); ++pass)
	{
		manager.addPass(**pass);
	}
	
	manager.runOnModule();*/
	device->select();
	device->load(&module->second);
	device->setOptimizationLevel(_optimization);
	device->unselect();
}

void opencl::OpenCLRuntime::_registerModule(const std::string& name, executive::Device * device) {
	ModuleMap::iterator module = _modules.find(name);
	if(module != _modules.end()) {
		_registerModule(module, device);
	}
}

void opencl::OpenCLRuntime::_registerAllModules(executive::Device * device) {
	for(ModuleMap::iterator module = _modules.begin(); 
		module != _modules.end(); ++module) {
		_registerModule(module, device);
	}
}

void opencl::OpenCLRuntime::_mapKernelParameters(Kernel & kernel, Device * device) {

	const Program & prog = *(kernel.program);
	assert(prog.ptxModule.find(device) != prog.ptxModule.end());

	const std::string & moduleName = prog.ptxModule.find(device)->second;
	assert(_modules.find(moduleName) != _modules.end());

	const std::string & kernelName = kernel.name;
	const ir::Kernel * k = _modules.find(moduleName)->second.getKernel(kernelName);

	if(k->arguments.size() != kernel.parameterSizes.size())
		throw CL_INVALID_KERNEL_ARGS;

	//Get aligned parameter sizes	
	kernel.parameterBlockSize = 0;
	kernel.parameterOffsets.clear();
	unsigned int argId = 0;
	for (ir::Kernel::ParameterVector::const_iterator parameter = k->arguments.begin(); 
		parameter != k->arguments.end(); ++parameter, ++argId) {
		kernel.parameterOffsets[argId] = kernel.parameterBlockSize;
		unsigned int misalignment = (kernel.parameterBlockSize) % parameter->getAlignment();
		unsigned int alignmentOffset = misalignment == 0 
			? 0 : parameter->getAlignment() - misalignment;
		kernel.parameterBlockSize += alignmentOffset;
		kernel.parameterBlockSize += parameter->getSize();
	}

	if(kernel.parameterBlock)
	{
		delete[] kernel.parameterBlock;
		kernel.parameterBlock = NULL;
	}
	try {
		kernel.parameterBlock = new char[kernel.parameterBlockSize];
		assert(kernel.parameterBlock);
		memset(kernel.parameterBlock, 0, kernel.parameterBlockSize);
	}
	catch(...) {
		throw CL_OUT_OF_HOST_MEMORY;
	}

	//Copy parameters to aligned offset
	assert(kernel.parameterSizes.size() == kernel.parameterPointers.size());
	assert(kernel.parameterSizes.size() == kernel.parameterOffsets.size());
	argId = 0;
	for(SizeMap::iterator size = kernel.parameterSizes.begin();
		size != kernel.parameterSizes.end(); size++, argId++) {
		assert(size->first == argId);
		assert(kernel.parameterPointers.find(argId) != kernel.parameterPointers.end());
		assert(kernel.parameterOffsets.find(argId) != kernel.parameterOffsets.end());
		
		unsigned int offset = kernel.parameterOffsets[argId];
		size_t oriSize = size->second;
		size_t argSize = k->arguments[argId].getSize();
		void * pointer = kernel.parameterPointers[argId];

		//check if it is a memory address argument
		if(oriSize == sizeof(cl_mem) &&  
			std::find(_memories.begin(), _memories.end(), *(cl_mem *)pointer) != _memories.end()) {//pointer is memory object
		
			if(argSize != sizeof(void *))
				throw CL_INVALID_KERNEL_ARGS;

			MemoryObject * mem = *(cl_mem *)pointer;
			if(mem->allocations.find(device) == mem->allocations.end())
				throw CL_MEM_OBJECT_ALLOCATION_FAILURE;

			void * addr = mem->allocations[device]->pointer();
			memcpy(kernel.parameterBlock + offset, &addr, argSize);
		}
		else { //non-memory argument
			if(oriSize != argSize)
				throw CL_INVALID_KERNEL_ARGS;

			memcpy(kernel.parameterBlock + offset, pointer, argSize);
		}

	}
}
////////////////////////////////////////////////////////////////////////////////

opencl::OpenCLRuntime::OpenCLRuntime() : _inExecute(false), _deviceCount(0),
	_devicesLoaded(false), 
	/*_selectedDevice(NULL),*/ _nextSymbol(1), _computeCapability(2), _flags(0), 
	_optimization((translator::Translator::OptimizationLevel)
		config::get().executive.optimizationLevel) {

	// get device count
	if(config::get().executive.enableNVIDIA) {
		_deviceCount += executive::Device::deviceCount(
			ir::Instruction::SASS, _computeCapability);
	}
	if(config::get().executive.enableEmulated) {
		_deviceCount += executive::Device::deviceCount(
			ir::Instruction::Emulated, _computeCapability);
	}
	if(config::get().executive.enableLLVM) {
		_deviceCount += executive::Device::deviceCount(
			ir::Instruction::LLVM, _computeCapability);
	}
	if(config::get().executive.enableAMD) {
		_deviceCount += executive::Device::deviceCount(
			ir::Instruction::CAL, _computeCapability);
	}
	if(config::get().executive.enableRemote) {
		_deviceCount += executive::Device::deviceCount(
			ir::Instruction::Remote, _computeCapability);
	}
}

opencl::OpenCLRuntime::~OpenCLRuntime() {
	//
	// free things that need freeing
	//
	// devices
	for (DeviceList::iterator device = _devices.begin(); 
		device != _devices.end(); ++device) {
		delete (*device)->exeDevice;
		delete *device;
	}
	
	// mutex

	// contexts
	for(ContextList::iterator context = _contexts.begin();
		context != _contexts.end(); ++context) {
		delete *context;
	}
	
	// textures
	
	// programs
	for (ProgramList::iterator program = _programs.begin();
		program != _programs.end(); ++program) {
		delete *program;
	}

	// kernels
	for (KernelList::iterator kernel = _kernels.begin();
		kernel != _kernels.end(); ++kernel) {
		delete *kernel;
	}

	// memory objects
	for (MemoryObjectList::iterator memory = _memories.begin();
		memory != _memories.end(); ++memory) {
		delete *memory;
	}

	// command queues
	for (CommandQueueList::iterator queue = _queues.begin();
		queue != _queues.end(); ++queue) {
		delete *queue;
	}
	
	// fat binaries
	
	// config
	config::destroy();
	
	// globals
}



////////////////////////////////////////////////////////////////////////////////
/*
void opencl::OpenCLRuntime::addTraceGenerator( trace::TraceGenerator& gen,
	bool persistent ) {
	_lock();
	Context& thread = _createContext();
	if (persistent) {
		thread.persistentTraceGenerators.push_back(&gen);
	}
	else {
		thread.nextTraceGenerators.push_back(&gen);
	}
	_unlock();
}

void opencl::OpenCLRuntime::clearTraceGenerators() {
	_lock();
	Context& thread = _createContext();
	thread.persistentTraceGenerators.clear();
	thread.nextTraceGenerators.clear();
	_unlock();
}

void opencl::OpenCLRuntime::addPTXPass(transforms::Pass &pass) {
	_lock();
	_passes.insert(&pass);
	_unlock();
}

void opencl::OpenCLRuntime::removePTXPass(transforms::Pass &pass) {
	_lock();

	assert(_passes.count(&pass) != 0);
	_passes.erase(&pass);

	_unlock();
}

void opencl::OpenCLRuntime::clearPTXPasses() {
	_lock();
	_passes.clear();
	_unlock();
}
*/
void opencl::OpenCLRuntime::limitWorkerThreads(unsigned int limit) {
	_lock();
	for (DeviceList::iterator device = _devices.begin(); 
		device != _devices.end(); ++device) {
		(*device)->exeDevice->limitWorkerThreads(limit);
	}
	_unlock();
}

void opencl::OpenCLRuntime::registerPTXModule(std::istream& ptx, 
	const std::string& name) {
	_lock();
	report("Loading module (ptx) - " << name);
	assert(_modules.count(name) == 0);
	
	ModuleMap::iterator module = _modules.insert(
		std::make_pair(name, ir::Module())).first;
	
	std::string temp;
	
	ptx.seekg(0, std::ios::end);
	size_t size = ptx.tellg();
	ptx.seekg(0, std::ios::beg);
	
	temp.resize(size);
	ptx.read((char*)temp.data(), size);
	
	try {
		module->second.lazyLoad(temp, name);
	}
	catch(...) {
		_unlock();
		_modules.erase(module);
		throw;
	}
		
	_unlock();
}

/*
void opencl::OpenCLRuntime::registerTexture(const void* texref,
	const std::string& moduleName,
	const std::string& textureName, bool normalize) {
	_lock();
	
	report("registerTexture('" << textureName << ", norm: " << normalize );

	_textures[(void*)texref] = RegisteredTexture(moduleName,
		textureName, normalize);
	
	_unlock();
}
*/

void opencl::OpenCLRuntime::clearErrors() {
#if 0
	_lock();
	_unlock();
#endif
}

void opencl::OpenCLRuntime::reset() {
#if 0
	_lock();
	report("Resetting opencl runtime.");
	//_dimensions.clear();
	
	for(ContextList::iterator context = _contexts.begin(); 
		context != _contexts.end(); ++context)
	{
		report(" Delete context - ");
		delete *context;
	}

	for(DeviceVector::iterator device = _devices.begin(); 
		device != _devices.end(); ++device)
	{
		report(" Clearing memory on device - " << (*device)->properties().name);
		(*device)->clearMemory();
	}
	
	for(ModuleMap::iterator module = _modules.begin(); module != _modules.end();
		module != _modules.end())
	{
		bool found = false;
		report(" Unloading module - " << module->first);
		/*for(FatBinaryVector::iterator bin = _fatBinaries.begin(); 
			bin != _fatBinaries.end(); ++bin)
		{
			if(bin->name() == module->first)
			{
				found = true;
				break;
			}
		}*/
		
		if(!found)
		{
			for(DeviceVector::iterator device = _devices.begin(); 
				device != _devices.end(); ++device)
			{
				(*device)->select();
				(*device)->unload(module->first);
				(*device)->unselect();
			}
			
			_modules.erase(module++);
		}
		else
		{
			++module;
		}
	}
	_unlock();
#endif
}

ocelot::PointerMap opencl::OpenCLRuntime::contextSwitch(unsigned int destinationId, 
	unsigned int sourceId) {
#if 0
	report("Context switching from " << sourceId << " to " << destinationId);
	
	if(!_devicesLoaded) return ocelot::PointerMap();
	
	ocelot::PointerMap mappings;

	_lock();
	
	if(sourceId >= _devices.size())
	{
		_unlock();
		Ocelot_Exception("Invalid source device - " << sourceId);
	}
	
	if(destinationId >= _devices.size())
	{
		_unlock();
		Ocelot_Exception("Invalid destination device - " << destinationId);
	}
	
	executive::Device& source = *_devices[sourceId];
	executive::Device& destination = *_devices[destinationId];
	
	_unlock();
	
	source.select();
	executive::Device::MemoryAllocationVector sourceAllocations = 
		source.getAllAllocations();
	source.unselect();
		
	for(executive::Device::MemoryAllocationVector::iterator 
		allocation = sourceAllocations.begin();
		allocation != sourceAllocations.end(); ++allocation)
	{
		size_t size = (*allocation)->size();
		void* pointer = (*allocation)->pointer();
		
		if(!(*allocation)->global())
		{
			char* temp = new char[size];
			source.select();
			(*allocation)->copy(temp, 0, size);
			source.free(pointer);
			source.unselect();

			destination.select();
			executive::Device::MemoryAllocation* dest = destination.allocate(
				size);
			dest->copy(0, temp, size);
			destination.unselect();
			
			report(" Mapping device allocation at " << pointer 
				<< " to " << dest->pointer());
			mappings.insert(std::make_pair(pointer,	dest->pointer()));
			delete[] temp;
		}
		else if((*allocation)->host())
		{
			destination.select();
			executive::Device::MemoryAllocation* dest = 
				destination.allocateHost(size, (*allocation)->flags());
			dest->copy(0, pointer, size);
			destination.unselect();

			mappings.insert(std::make_pair(pointer, dest->pointer()));
			
			source.select();
			source.free(pointer);
			source.unselect();
		}
	}

	for(ModuleMap::iterator module = _modules.begin(); 
		module != _modules.end(); ++module)
	{
		if( !module->second.loaded() ) continue;
		for(ir::Module::GlobalMap::const_iterator 
			global = module->second.globals().begin();
			global != module->second.globals().end(); ++global)
		{
			source.select();
			executive::Device::MemoryAllocation* sourceGlobal = 
				source.getGlobalAllocation(module->first, global->first);
			assert(sourceGlobal != 0);
			source.unselect();

			destination.select();
			executive::Device::MemoryAllocation* destinationGlobal = 
				destination.getGlobalAllocation(module->first, global->first);
			assert(destinationGlobal != 0);
			destination.unselect();
			
			char* temp = new char[sourceGlobal->size()];
			source.select();
			sourceGlobal->copy(temp, 0, sourceGlobal->size());
			source.unselect();

			destination.select();
			destinationGlobal->copy(0, temp, destinationGlobal->size());
			destination.unselect();
			delete[] temp;
		}
	}
		
	_unlock();
	
	return mappings;
#endif
	return ocelot::PointerMap();
}

void opencl::OpenCLRuntime::unregisterModule(const std::string& name) {
	_lock();
	ModuleMap::iterator module = _modules.find(name);
	if (module == _modules.end()) {
		_unlock();
		Ocelot_Exception("Module - " << name << " - is not registered.");
	}
	
	for (DeviceList::iterator device = _devices.begin(); 
		device != _devices.end(); ++device) {
		(*device)->exeDevice->select();
		(*device)->exeDevice->unload(name);
		(*device)->exeDevice->unselect();
	}
	
	_modules.erase(module);
	
	_unlock();
}

static ir::Dim3 convert(const size_t d[3]) {
	return std::move(ir::Dim3(d[0], d[1], d[2]));
}

void opencl::OpenCLRuntime::_launchKernel(Kernel &kernel, Device * device)
{
	const std::string & kernelName = kernel.name;
	assert(kernel.program->built);
	assert(kernel.program->ptxModule.find(device) != kernel.program->ptxModule.end());
	const std::string & moduleName = (kernel.program)->ptxModule[device];

	report("kernel launch (" << kernelName 
		<< ") on device " << device);
	
	try {
		Context &ctx = *((Context *) kernel.context);
		trace::TraceGeneratorVector traceGens;

		traceGens = ctx.persistentTraceGenerators;
		traceGens.insert(traceGens.end(),
			ctx.nextTraceGenerators.begin(), 
			ctx.nextTraceGenerators.end());

		_inExecute = true;

		device->exeDevice->launch(moduleName, kernelName, convert(kernel.workGroupNum), 
			convert(kernel.localWorkSize), /*launch.sharedMemory*/0, 
			kernel.parameterBlock, kernel.parameterBlockSize, traceGens, NULL/*&_externals*/);
		_inExecute = false;
		report(" launch completed successfully");	
	}
	catch( const executive::RuntimeException& e ) {
		std::cerr << "==Ocelot== PTX Emulator failed to run kernel \"" 
			<< kernelName 
			<< "\" with exception: \n";
		std::cerr << _formatError( e.toString() ) 
			<< "\n" << std::flush;
		_inExecute = false;
		throw;
	}
	catch( const std::exception& e ) {
		std::cerr << "==Ocelot== " << device->exeDevice->properties().name
			<< " failed to run kernel \""
			<< kernelName
			<< "\" with exception: \n";
		std::cerr << _formatError( e.what() )
			<< "\n" << std::flush;
		throw;
	}
	catch(...) {
		throw;
	}
}


void opencl::OpenCLRuntime::setOptimizationLevel(
	translator::Translator::OptimizationLevel l) {
	_lock();

	_optimization = l;
	for (DeviceList::iterator device = _devices.begin(); 
		device != _devices.end(); ++device) {
		(*device)->exeDevice->select();
		(*device)->exeDevice->setOptimizationLevel(l);
		(*device)->exeDevice->unselect();
	}

	_unlock();
}
/*
void opencl::OpenCLRuntime::registerExternalFunction(const std::string& name,
	void* function) {
	
	_lock();

	report("Adding external function '" << name << "'");
	_externals.add(name, function);

	_unlock();
}

void opencl::OpenCLRuntime::removeExternalFunction(const std::string& name) {
	_lock();

	report("Removing external function '" << name << "'");

	_externals.remove(name);

	_unlock();	
}
*/
////////////////////////////////////////////////////////////////////////////////

cl_int opencl::OpenCLRuntime::clGetPlatformIDs(cl_uint num_entries, 
	cl_platform_id * platforms, 
	cl_uint * num_platforms) {
	cl_int result = CL_SUCCESS;
	_lock();

	try {
		if((num_entries == 0 && platforms != NULL) || (num_platforms == NULL && platforms == NULL))
			throw CL_INVALID_VALUE;
		
		_enumeratePlatforms();

		if(num_platforms)
			*num_platforms = _platforms.size();

		if(platforms) {
			PlatformList::iterator p = _platforms.begin();
			for(cl_uint i = 0; i < std::min((cl_uint)_platforms.size(), num_entries); 
				i++, p++)
				platforms[i] = (cl_platform_id)*p;
		}

	}
	catch(cl_int exception) {
		result = exception;
	}
	catch(...) {
		result = CL_OUT_OF_HOST_MEMORY;
	}
	_unlock();
    return result;
}

cl_int opencl::OpenCLRuntime::clGetPlatformInfo(cl_platform_id platform, 
	cl_platform_info param_name,
	size_t param_value_size, 
	void * param_value,
	size_t * param_value_size_ret) {
	cl_int result = CL_SUCCESS;
	_lock();

	try {
		if(std::find(_platforms.begin(), _platforms.end(), platform) == _platforms.end())
			throw CL_INVALID_PLATFORM;

		if(!param_value && !param_value_size_ret)
			throw CL_INVALID_VALUE;
		
		cl_int err;
		if((err = platform->getInfo(param_name,
			param_value_size,
			param_value,
			param_value_size_ret)) != CL_SUCCESS)
			throw err;

	}
	catch(cl_int exception) {
		result = exception;
	}
	catch(...) {
		result = CL_OUT_OF_HOST_MEMORY;
	}
	_unlock();
	return result;
}
	

cl_int opencl::OpenCLRuntime::clGetDeviceIDs(cl_platform_id platform, 
    cl_device_type device_type, 
    cl_uint num_entries,
    cl_device_id * devices,
    cl_uint * num_devices) {
	
	cl_int result = CL_SUCCESS;

	_lock();

	try {
		if(std::find(_platforms.begin(), _platforms.end(), platform) == _platforms.end())
			throw CL_INVALID_PLATFORM;
	
	
		if(device_type < CL_DEVICE_TYPE_CPU || device_type > CL_DEVICE_TYPE_ALL)
			throw CL_INVALID_DEVICE_TYPE;
		
		if((num_entries == 0 && devices != NULL) || (num_devices == NULL && devices == NULL))
			throw CL_INVALID_VALUE;
 
		if (_devices.empty())
			throw CL_DEVICE_NOT_FOUND;
	
		cl_uint j = 0;
		if(devices != 0) {
			for(DeviceList::iterator d = _devices.begin();
				d != _devices.end() && j < num_entries; d++) {
				if((device_type == CL_DEVICE_TYPE_ALL ||
					(*d)->type() == device_type)
					&& (*d)->platform() == platform) {
					devices[j] = (cl_device_id)(*d);
					j++;
				}
			}
		}
		
		if(num_devices != 0)
			*num_devices = j;
	
	}
	catch(cl_int exception) {
		result = exception;
	}
	catch(...) {
		result = CL_OUT_OF_HOST_MEMORY;
	}
	_unlock();
    return result;
}

cl_int opencl::OpenCLRuntime::clGetDeviceInfo(cl_device_id device,
	cl_device_info param_name,
	size_t param_value_size,
	void * param_value,
	size_t * param_value_size_ret) {
	cl_int result = CL_SUCCESS;
	_lock();
	
	try {

		if(device == NULL || std::find(_devices.begin(), _devices.end(), device) == _devices.end())
			throw CL_INVALID_DEVICE;

		if(!param_value && !param_value_size_ret)
			throw CL_INVALID_VALUE;

		cl_int err = device->getInfo(param_name, param_value_size, param_value,
			param_value_size_ret);
		if(err != CL_SUCCESS)
			throw err;

	}
	catch(cl_int exception) {
		result = exception;
	}
	catch(...) {
		result = CL_OUT_OF_HOST_MEMORY;
	}	

	_unlock();
	return result;
}

cl_context opencl::OpenCLRuntime::clCreateContext(const cl_context_properties * properties,
	cl_uint num_devices,
	const cl_device_id * devices,
	void (CL_CALLBACK * pfn_notify)(const char *, const void *, size_t, void *),
	void * user_data,
	cl_int * errcode_ret) {
	
	_lock();
	cl_int err = CL_SUCCESS;

	Context * ctx = NULL;

	try {
		if(properties && properties[0] && (properties[0] != CL_CONTEXT_PLATFORM 
			|| properties[2] != 0 /*properties terminates with 0*/ ))
			throw CL_INVALID_PROPERTY;
		
		if(std::find(_platforms.begin(), _platforms.end(), (cl_platform_id)properties[1]) 
			== _platforms.end() )
			throw CL_INVALID_PLATFORM;

		if(devices == 0 || num_devices == 0
			|| (pfn_notify == 0 && user_data != 0))
			throw CL_INVALID_VALUE;
		if(pfn_notify) {
			assertM(false, "call_back function unsupported\n");
			throw CL_UNIMPLEMENTED;
		}
		
		ctx = _createContext();
		for (cl_uint i = 0; i < num_devices; i++) {
			if(devices[i] == NULL 
				|| std::find(_devices.begin(), _devices.end(), devices[i]) == _devices.end()) {
				err = CL_INVALID_DEVICE;
				break;
			}
			ctx->validDevices.push_back(devices[i]);
		}
	}
	catch(cl_int exception) {
		err = exception;
	}
	catch(...) {
		err = CL_OUT_OF_HOST_MEMORY;
	}

	_unlock();

	if(errcode_ret)
		*errcode_ret = err;
	return (cl_context)(ctx);
}

cl_command_queue opencl::OpenCLRuntime::clCreateCommandQueue(cl_context context, 
	cl_device_id device, 
	cl_command_queue_properties properties,
	cl_int * errcode_ret) {
	
	CommandQueue * queue = NULL;
	
	_lock();
	cl_int err = CL_SUCCESS;

	try {
		if(std::find(_contexts.begin(), _contexts.end(), context) == _contexts.end())
			throw CL_INVALID_CONTEXT;
		
		if(std::find(context->validDevices.begin(), context->validDevices.end(), device) 
			== context->validDevices.end())//Not found
			throw CL_INVALID_DEVICE;
	
		if(properties > (CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE |
						CL_QUEUE_PROFILING_ENABLE))
			throw CL_INVALID_VALUE;
		
		if((properties & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE) 
			||(properties & CL_QUEUE_PROFILING_ENABLE)) {
			assertM(false, "unimplemented queue properties");
			throw CL_UNIMPLEMENTED;
		}

		device->exeDevice->select();
		unsigned int stream;
		try {
			stream = device->exeDevice->createStream();
		}
		catch(...) {
			device->exeDevice->unselect();
			throw CL_OUT_OF_RESOURCES;
		}
		device->exeDevice->unselect();

		try {
			queue = new CommandQueue(context, device, properties, stream);
			_queues.push_back(queue);
		}
		catch(...) {
			throw CL_OUT_OF_HOST_MEMORY;
		}
		context->validQueues.push_back(queue);
		
	}
	catch(cl_int exception) {
		err = exception;
	}
	catch(...) {
		err = CL_OUT_OF_HOST_MEMORY;
	}
	if(errcode_ret)
		*errcode_ret = err;
	_unlock();
	return (cl_command_queue)queue;
}

cl_program opencl::OpenCLRuntime::clCreateProgramWithSource(cl_context context,
	cl_uint count,
	const char ** strings,
	const size_t * lengths,
	cl_int * errcode_ret) {
	
	_lock();
	cl_int err = CL_SUCCESS;
	Program * program = NULL;

	try {
		if(std::find(_contexts.begin(), _contexts.end(), context) == _contexts.end())
			throw CL_INVALID_CONTEXT;
		
		if(count == 0 || strings == 0)
			throw CL_INVALID_VALUE;
		
		cl_uint i;
		for(i = 0; i < count; i++) {
			if(strings[i] == 0) {
				throw CL_INVALID_VALUE;
			}
		}	
		
		std::stringstream stream;
		for(i = 0; i < count; i++)
			stream << strings[i];

		program = new Program(stream.str(), context);
		_programs.push_back(program);
		context->validPrograms.push_back(program);
	}
	catch(cl_int exception) {
		err = exception;
	}
	catch(...) {
		err = CL_OUT_OF_HOST_MEMORY;
	}

	if(errcode_ret)
		*errcode_ret = err;
	_unlock();

	return (cl_program)program;
}

cl_int opencl::OpenCLRuntime::clBuildProgram(cl_program program,
	cl_uint num_devices,
	const cl_device_id * device_list,
	const char * options, 
	void (CL_CALLBACK * pfn_notify)(cl_program, void *),
	void * user_data) {
	cl_int result = CL_SUCCESS;
	_lock();
	try {
		
		if(std::find(_programs.begin(), _programs.end(), program) == _programs.end())//Not found
			throw CL_INVALID_PROGRAM;

		if((num_devices == 0 && device_list) || (num_devices && device_list == NULL))
			throw CL_INVALID_VALUE;

		if(pfn_notify == NULL && user_data)
			throw CL_INVALID_VALUE;

		for(cl_uint i = 0; i < num_devices; i++) {
			if(std::find(program->context->validDevices.begin(), 
				program->context->validDevices.end(),
				device_list[i]) == program->context->validDevices.end()) {//Not found
				throw CL_INVALID_DEVICE;
				break;
			}
		}

		if(options || pfn_notify || user_data) {
			assertM(false, "unsupported options, pfn_nofify or user_data arguments");
			throw CL_UNIMPLEMENTED;
		}

		//For source building, since we don't have opencl frontend, load buildout.ptx instead
		if(!program->ptxModule.empty() && !program->built) {
			assertM(false, "unsupported binary. note that clCreateProgramWithBinaries is currently unsupported!");
			throw CL_UNIMPLEMENTED;
		}

		if(!program->built) {
			//Not built is loaded
			std::ifstream ptx("buildout.ptx", std::ifstream::in); //Temorarily load ptx file as built binary
			if(ptx.fail()) {
				assertM(false, "buildout.ptx not found, run opencl programram with libOpenCL first!");
				throw CL_BUILD_PROGRAM_FAILURE;
			}
				
			std::string temp;
		
			ptx.seekg(0, std::ios::end);
			size_t size = ptx.tellg();
			ptx.seekg(0, std::ios::beg);
			
			if(!size) {
				assertM(false, "buildout.ptx is empty!");
				throw CL_BUILD_PROGRAM_FAILURE;
			}
	
			temp.resize(size);
			ptx.read((char*)temp.data(), size);
	
			// Register ptx with device
			DeviceList devices;
			if(num_devices) {
				for(cl_uint i = 0; i < num_devices; i++)
					devices.push_back((device_list[i]));
			}
			else
				devices = program->context->validDevices;
	
			try{
				for(DeviceList::iterator d = devices.begin(); d != devices.end(); d++) {
					std::stringstream moduleName;
					moduleName << program->name << "_" << (*d)->exeDevice->properties().name;
					report("Loading module (ptx) - " << moduleName.str());
	
					std::string name = moduleName.str();
					assert(_modules.count(name) == 0);
			
					ModuleMap::iterator module = _modules.insert(
						std::make_pair(name, ir::Module())).first;
				
					module->second.lazyLoad(temp, name);
			
					_registerModule(module, (*d)->exeDevice);
	
					assert(program->ptxModule.count(*d) == 0);
					program->ptxModule.insert(std::make_pair(*d, name));
		
					assert(program->ptxCode.count(*d) == 0);
					program->ptxCode.insert(std::make_pair(*d, temp));
				}
	
				assert(program->ptxModule.size() == program->ptxCode.size());
				program->built = true;
			}
			catch(...) {
				throw CL_BUILD_PROGRAM_FAILURE;
			}
		}
								
	}
	catch(cl_int exception) {
		result = exception;
	}
	catch(...) {
		result = CL_BUILD_PROGRAM_FAILURE;
	}
	
	_unlock();
	return result;
}

cl_int opencl::OpenCLRuntime::clGetProgramInfo(cl_program program,
	cl_program_info param_name,
	size_t param_value_size,
	void * param_value,
	size_t * param_value_size_ret) {
	cl_int result = CL_SUCCESS;
	_lock();
	
	try {
		
		if(param_name < CL_PROGRAM_REFERENCE_COUNT || param_name > CL_PROGRAM_BINARIES)
			throw CL_INVALID_VALUE;
		
		if(std::find(_programs.begin(), _programs.end(), program) == _programs.end())//Not found
			throw CL_INVALID_PROGRAM;

		switch(param_name) {
			case CL_PROGRAM_BINARY_SIZES: {
				if(param_value_size < program->context->validDevices.size() * sizeof(size_t))
					throw CL_INVALID_VALUE;

				if(param_value)	{
					DeviceList::iterator device;
					unsigned int i = 0;
					//get ptx code size for every valid device
					for(device = program->context->validDevices.begin(); 
							device != program->context->validDevices.end(); device++) {
						//if ptx code for a specific device does not exist, return 0
						std::map <Device *, std::string>::iterator ptx;
						if((ptx = program->ptxCode.find(*device)) != program->ptxCode.end())
							((size_t *)param_value)[i] = ptx->second.size();
						else
							((size_t *)param_value)[i] = 0;
						i++;
					}
				}

				if(param_value_size_ret)
					*param_value_size_ret = program->context->validDevices.size() * sizeof(size_t);
	
				break;
			}
			case CL_PROGRAM_BINARIES: {
				if(param_value_size < program->context->validDevices.size() * sizeof(char *))
					throw CL_INVALID_VALUE;

				if(param_value)	{
					DeviceList::iterator device;
					unsigned int i = 0;
					//get ptx code size for every valid device
					for(device = program->context->validDevices.begin(); 
							device != program->context->validDevices.end(); device++) {
							
						//if ptx code for a specific device does not exist, don't copy
						std::map <Device *, std::string>::iterator ptx;
						if((ptx = program->ptxCode.find(*device)) != program->ptxCode.end())
							std::memcpy(((char **)param_value)[i], ptx->second.data(), ptx->second.size());
						i++;
					}
				}

				if(param_value_size_ret)
					*param_value_size_ret = program->context->validDevices.size() * sizeof(size_t);
	
				break;
			}
				
				break;
			default:
				assertM(false, "unsupported program info");
				throw CL_UNIMPLEMENTED;
				break;
		}
	}
	catch(cl_int exception) {
		result = exception;
	}
	catch(...) {
		result = CL_OUT_OF_HOST_MEMORY;
	}

	_unlock();
	return result;
}

cl_kernel opencl::OpenCLRuntime::clCreateKernel(cl_program program,
	const char * kernel_name,
	cl_int * errcode_ret) {
	
	cl_int err = CL_SUCCESS;	

	Kernel * kernel = NULL;

	_lock();

	try{

		if(find(_programs.begin(), _programs.end(), program) == _programs.end())//Not found
			throw CL_INVALID_PROGRAM;

		if(!program->built)
			throw CL_INVALID_PROGRAM_EXECUTABLE;

		if(!kernel_name)
			throw CL_INVALID_VALUE;

		std::map < Device *, std::string >::iterator module;
		std::string kernelName(kernel_name);
		
		for(module = program->ptxModule.begin(); module != program->ptxModule.end(); module++) {
			ModuleMap::iterator m = _modules.find(module->second);
			assert(m != _modules.end());
			if(m->second.getKernel(kernelName)==0) {//kernel not found
				throw CL_INVALID_KERNEL_NAME;
				break;
			}
	
		}

		report("Registered kernel - " << kernelName
			<< " in program '" << program->name << "'");

		try {
			kernel = new Kernel(kernelName, program, program->context);
			_kernels.push_back(kernel);
		}
		catch(...) {
			throw CL_OUT_OF_HOST_MEMORY;
		}
		program->kernels.push_back(kernel);

	}
	catch(cl_int exception) {
		err = exception;
	}
	catch(...) {
		err = CL_OUT_OF_HOST_MEMORY;
	}

	if(errcode_ret)
		*errcode_ret = err;
	_unlock();
	return (cl_kernel)kernel;
}

cl_mem opencl::OpenCLRuntime::clCreateBuffer(cl_context context,
	cl_mem_flags flags,
	size_t size,
	void * host_ptr,
	cl_int * errcode_ret) {

	BufferObject * buffer = NULL;
	_lock();
	cl_int err = CL_SUCCESS;

	try {
		if(find(_contexts.begin(), _contexts.end(), context) == _contexts.end())
			throw CL_INVALID_CONTEXT;

		if((flags & CL_MEM_READ_ONLY) 
			&& ((flags & CL_MEM_READ_WRITE) || (flags & CL_MEM_WRITE_ONLY)))
			throw CL_INVALID_VALUE;
		if((flags & CL_MEM_WRITE_ONLY) 
			&& ((flags & CL_MEM_READ_WRITE) || (flags & CL_MEM_READ_ONLY)))
			throw CL_INVALID_VALUE;

		if((flags & CL_MEM_ALLOC_HOST_PTR) && (flags & CL_MEM_COPY_HOST_PTR))
			throw CL_INVALID_VALUE;
		
		if(size == 0)
			throw CL_INVALID_BUFFER_SIZE;

		if((!host_ptr && ((flags & CL_MEM_USE_HOST_PTR) || (flags & CL_MEM_COPY_HOST_PTR)))
			|| (host_ptr && !(flags & CL_MEM_USE_HOST_PTR) && !(flags & CL_MEM_COPY_HOST_PTR)))
			throw CL_INVALID_HOST_PTR;

		if(host_ptr) {
			assertM(false, "unsuported host_ptr");
			throw CL_UNIMPLEMENTED;
		}

		std::map< Device *, executive::Device::MemoryAllocation * > allocations;
		for(DeviceList::iterator device = context->validDevices.begin();
			device != context->validDevices.end(); device++) {
			(*device)->exeDevice->select();
			try {
				executive::Device::MemoryAllocation * allocation =  (*device)->exeDevice->allocate(size);
				if(allocation == NULL) throw;
				allocations.insert(std::make_pair(*device, allocation));
				report("clCreateBuffer() return address = " <<  allocation->pointer() << ", size = " << size);
			}
			catch(...) {
				(*device)->exeDevice->unselect();
				throw CL_OUT_OF_RESOURCES;
			}

			(*device)->exeDevice->unselect();
		}

		try {
			buffer = new BufferObject(allocations, context, flags, size);
			_memories.push_back(buffer);
		}
		catch(...) {
			throw CL_OUT_OF_HOST_MEMORY;
		}
		context->validMemories.push_back(buffer);

	}
	catch(cl_int exception) {
		err = exception;
	}
	catch(...) {
		err = CL_OUT_OF_HOST_MEMORY;
	}

	if(errcode_ret)
		*errcode_ret = err;
	_unlock();
	return (cl_mem)buffer;
}

cl_int opencl::OpenCLRuntime::clEnqueueReadBuffer(cl_command_queue command_queue,
	cl_mem buffer,
	cl_bool blocking_read,
	size_t offset,
	size_t cb, 
	void * ptr,
	cl_uint num_events_in_wait_list,
	const cl_event * event_wait_list,
	cl_event * event) {
	cl_int result = CL_SUCCESS;
	_lock();

	try {
		
		if(std::find(_queues.begin(), _queues.end(), command_queue) == _queues.end())
			throw CL_INVALID_COMMAND_QUEUE;

		if(std::find(_memories.begin(), _memories.end(), buffer) == _memories.end())
			throw CL_INVALID_MEM_OBJECT;

		if(buffer->type() != CL_MEM_OBJECT_BUFFER)
			throw CL_INVALID_MEM_OBJECT;

		if(command_queue->context() != buffer->context())
			throw CL_INVALID_CONTEXT;

		if(offset >= buffer->size() || cb + offset > buffer->size())
			throw CL_INVALID_VALUE;
		
		if(ptr == NULL)
			throw CL_INVALID_VALUE;

		if(event_wait_list == NULL && num_events_in_wait_list > 0)
			throw CL_INVALID_EVENT_WAIT_LIST;

		if(event_wait_list && num_events_in_wait_list == 0)
			throw CL_INVALID_EVENT_WAIT_LIST;

		if(event_wait_list) {
			assertM(false, "non-null event wait list is no supported!");
			throw CL_UNIMPLEMENTED;
		}

		if(event) {
			assertM(false, "non-null event is not supported!");
			throw CL_UNIMPLEMENTED;
		}

		if(blocking_read == false) {
			assertM(false, "unblocking read is not supported!");
			throw CL_UNIMPLEMENTED;
		}

		std::map<Device *, executive::Device::MemoryAllocation *> & allocations = buffer->allocations;
		Device * device = command_queue->device();
		if(allocations.find(device) == allocations.end() || allocations.find(device)->second == NULL)
			throw CL_MEM_OBJECT_ALLOCATION_FAILURE;

		executive::Device::MemoryAllocation * allocation = allocations.find(device)->second;
		assert(device->exeDevice->checkMemoryAccess((char *)allocation->pointer() + offset, cb));
		allocation->copy(ptr, offset, cb);

	}
	catch(cl_int exception) {
		result = exception;
	}
	catch(...) {
		result = CL_OUT_OF_HOST_MEMORY;
	}
	_unlock();
	return result;
}

cl_int opencl::OpenCLRuntime::clEnqueueWriteBuffer(cl_command_queue command_queue,
	cl_mem buffer,
	cl_bool blocking_write,
	size_t offset,
	size_t cb, 
	const void * ptr,
	cl_uint num_events_in_wait_list,
	const cl_event * event_wait_list,
	cl_event * event) {
	cl_int result = CL_SUCCESS;
	_lock();

	try {

		if(std::find(_queues.begin(), _queues.end(), command_queue) == _queues.end())
			throw CL_INVALID_COMMAND_QUEUE;

		if(std::find(_memories.begin(), _memories.end(), buffer) == _memories.end())
			throw CL_INVALID_MEM_OBJECT;

		if(command_queue->context() != buffer->context())
			throw CL_INVALID_CONTEXT;

		if(offset >= buffer->size() || cb + offset > buffer->size())
			throw CL_INVALID_VALUE;
		
		if(ptr == NULL)
			throw CL_INVALID_VALUE;

		if(event_wait_list == NULL && num_events_in_wait_list > 0)
			throw CL_INVALID_EVENT_WAIT_LIST;

		if(event_wait_list && num_events_in_wait_list == 0)
			throw CL_INVALID_EVENT_WAIT_LIST;

		if(event_wait_list) {
			assertM(false, "non-null event wait list is no supported!");
			throw CL_UNIMPLEMENTED;
		}

		if(event) {
			assertM(false, "non-null event is not supported!");
			throw CL_UNIMPLEMENTED;
		}

		if(blocking_write == false) {
			assertM(false, "unblocking write is not supported!");
			throw CL_UNIMPLEMENTED;
		}

		std::map<Device *, executive::Device::MemoryAllocation *> & allocations = buffer->allocations;
		Device * device = command_queue->device();
		if(allocations.find(device) == allocations.end() || allocations.find(device)->second == NULL)
			throw CL_MEM_OBJECT_ALLOCATION_FAILURE;

		executive::Device::MemoryAllocation * allocation = allocations.find(device)->second;
		assert(device->exeDevice->checkMemoryAccess((char *)allocation->pointer() + offset, cb));
		
		allocation->copy(offset, ptr, cb);

	}
	catch(cl_int exception) {
		result = exception;
	}
	catch(...) {
		result = CL_OUT_OF_HOST_MEMORY;
	}
	_unlock();
	return result;
}

cl_int opencl::OpenCLRuntime::clSetKernelArg(cl_kernel kernel,
	cl_uint arg_index,
	size_t arg_size,
	const void * arg_value) {
	cl_int result = CL_SUCCESS;

	_lock();

	try {
		if(std::find(_kernels.begin(), _kernels.end(), kernel) == _kernels.end())
			throw CL_INVALID_KERNEL;

		kernel->parameterSizes[arg_index] = arg_size;
		char * paramVal = new char[arg_size];
		memcpy(paramVal, arg_value, arg_size);
		kernel->parameterPointers[arg_index] = paramVal;
		
		
	}
	catch(cl_int exception) {
		result = exception;
	}
	catch(...) {
		result = CL_OUT_OF_HOST_MEMORY;
	}

	_unlock();
	return result;
}

cl_int opencl::OpenCLRuntime::clEnqueueNDRangeKernel(cl_command_queue command_queue,
	cl_kernel kernel,
	cl_uint work_dim,
	const size_t * global_work_offset,
	const size_t * global_work_size,
	const size_t * local_work_size,
	cl_uint num_events_in_wait_list,
	const cl_event * event_wait_list,
	cl_event * event) {
	cl_int result = CL_SUCCESS;

	_lock();

	try {
		if(std::find(_queues.begin(), _queues.end(), command_queue) == _queues.end())
			throw CL_INVALID_COMMAND_QUEUE;

		if(std::find(_kernels.begin(), _kernels.end(), kernel) == _kernels.end())
			throw CL_INVALID_KERNEL;

		if(command_queue->context() != kernel->context)
			throw CL_INVALID_CONTEXT;
		
		if(kernel->parameterSizes.size() == 0)
			throw CL_INVALID_KERNEL_ARGS;

		assert(kernel->parameterSizes.size() == kernel->parameterPointers.size());
		
		_mapKernelParameters(*kernel, command_queue->device());

		if(work_dim < 1 || work_dim > 3)
			throw CL_INVALID_WORK_DIMENSION;

		if(global_work_size == NULL)
			throw CL_INVALID_GLOBAL_WORK_SIZE;

		for(cl_uint dim = 0; dim < work_dim; dim++) {
			if (global_work_size[dim] == 0)
				throw CL_INVALID_GLOBAL_WORK_SIZE;
		}

		if(global_work_offset != NULL) {
			assertM(false, "non-null global work offset unsupported");
			throw CL_UNIMPLEMENTED;
		}

		if(local_work_size) {
			for(cl_uint dim = 0; dim < work_dim; dim++) {
			if(local_work_size[dim] == 0)
				throw CL_INVALID_WORK_ITEM_SIZE;

			if (global_work_size[dim] / local_work_size[dim] * local_work_size[dim] != global_work_size[dim])
				throw CL_INVALID_WORK_GROUP_SIZE;
			}
		}

		if((event_wait_list == NULL && num_events_in_wait_list > 0) || (event_wait_list && num_events_in_wait_list == 0))
			throw CL_INVALID_EVENT_WAIT_LIST;


		for(cl_uint dim = 0; dim < 3; dim++) {
			if(dim < work_dim) {
				kernel->globalWorkSize[dim] = global_work_size[dim];
				if(local_work_size)
					kernel->localWorkSize[dim] = local_work_size[dim];
				else
					kernel->localWorkSize[dim] = global_work_size[dim];
			}
			else {
				kernel->globalWorkSize[dim] = 1;
				kernel->localWorkSize[dim] = 1;
			}

			kernel->workGroupNum[dim] = kernel->globalWorkSize[dim] / kernel->localWorkSize[dim];
		}

		try {
			_launchKernel(*kernel, command_queue->device());
		}
		catch(...) {
			throw CL_OUT_OF_RESOURCES;
		}
	}
	catch(cl_int exception) {
		result = exception;
	}
	catch(...) {
		result = CL_OUT_OF_HOST_MEMORY;
	}

	_unlock();
	return result;
}
