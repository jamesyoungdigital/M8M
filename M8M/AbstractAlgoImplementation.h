/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <functional>
#include "AlgoImplementationInterface.h"
#include "../Common/hashing.h"
#include <fstream>

template<typename MiningProcessorsProvider>
class AbstractAlgoImplementation : public AlgoImplementationInterface {
    const char *name;
	const char *version;
public:
	const bool littleEndianAlgo; //!< if this is false, the merkle root will be flipped after building by MakeNoncedHeader. This is the case most of the time.
	typename MiningProcessorsProvider::ErrorFunc errorCallback;
	AbstractAlgoImplementation(const char *caseInsensitiveName, const char *presentationVersionString, typename MiningProcessorsProvider::ErrorFunc f, bool le)
		: name(caseInsensitiveName), version(presentationVersionString), errorCallback(f), littleEndianAlgo(le) { }
	~AbstractAlgoImplementation() { }
    
	bool AreYou(const char *name) const { return !_stricmp(name, this->name); }
	std::string GetName() const { return std::string(name); }
	aulong GetVersioningHash() const { 
		std::string sign(version);
		sign += CustomVersioningStrings();
		hashing::SHA256 blah(reinterpret_cast<const aubyte*>(sign.c_str()), sign.length());
		hashing::SHA256::Digest blobby;
		blah.GetHash(blobby);
		aulong ret = 0; // ignore endianess here so we get to know host endianess by algo signature
		for(asizei loop = 0; loop < blobby.size(); loop += 8) {
			aulong temp;
			memcpy_s(&temp, sizeof(temp), blobby.data() + loop, sizeof(temp));
			ret ^= temp;
		}
		return ret;
	}

	/*! Call this after a sequence of AddSettings call to have your devices mapped to the most appropriate config.*/
	virtual void SelectSettings(typename MiningProcessorsProvider::ComputeNodes &nodes) = 0;
	
	/* Probe all the available computing devices provided by the MiningProcessorProvider and figure out which set of parameters is more appropriate
	for each device. After each device has been assigned to its settings, allocate the required resources.
	\returns First element of the pair identifies the internal setting being used while the second is the number of concurrent algo instances
	being run with those settings. Those values were really produced by SelectSettings.
	\sa GetWaitEvents */
	virtual std::vector< std::pair<asizei, asizei> > GenResources(typename MiningProcessorsProvider::ComputeNodes &procs) = 0;

	//! Clear all the resources (but not the settings!) associated with this implementation, for all devices.
	virtual void Clear(MiningProcessorsProvider &api) = 0;
    
	//! Returns true if the internal algorithm-instance structure can consume input, thereby iterating again on the algorithm implemented.
	virtual bool CanTakeInput(asizei setIndex, asizei resIndex) const = 0;
    
	/*! Call this to set new input data for a given concurrent algorithm. Calling this is a valid operation only if CanTakeInput(...) returned true.
	This effectively works as a restart call for the given concurrent algorithm instance. */
    virtual auint BeginProcessing(asizei setIndex, asizei resIndex, const stratum::AbstractWorkUnit &wu, auint prevHashes) = 0;

	struct IterationStartInfo {
		std::string job;
		auint nonce2;
		std::array<aubyte, 128> header; //!< to support hash checking with ease
	};
    
	/*! Informs outer code some results are ready to be poured out really quick - no sync or expensive operations involved.
	When this returns false, results are not available and might not be for two main reasons:
	1- Algorithm requires multiple steps and only some of them have been executed.
	2- The algorithm is really complete but it is not possible to produce the results by quickly copying them to the results buffer. 
	The outer code can understand what to do according to the result of either GetWaitEvents or Dispatch(). */
	virtual bool ResultsAvailable(IterationStartInfo &wu, std::vector<auint> &results, asizei setIndex, asizei resIndex) = 0;
    
	/*! This is a valid call if ResultsAvailable returned false.
	In general, if the specific algorithm instance has something to do, this will return 0 and add nothing to the list.
	This must happen as long as there are input buffers available to consume to allow pipelining.

	Otherwise it returns the amount of wait handles added to the list.
	There's no guarantee on the order, the outer code will just sleep until at least one event in its pool (which could
	encompass different algorithms) will be marked as ready. Therefore, it could be possible for an algorithm to be
	spuriously woken up.
	\note This function must be lightweight, a mere getter conceptually. All structures required to make it work must
	be generated at the end of the last pass.
	\note The miner thread will collect a single set of wait events. It is therefore necessary those events to be coherent and meaningful
	as a whole. This does not make sense at this level of abstraction unfortunately: it's OpenCL stuff. Wait events to be in a single
	wait call must come from the same context. */
    virtual auint GetWaitEvents(std::vector<typename MiningProcessorsProvider::WaitEvent> &list, asizei setIndex, asizei resIndex) const = 0;

    //! Try to advance the tasks one step. Implementations should try to not cause the thread to block.
	//! \return false if this specific algorithm essentially completed and is waiting for results to become available.
    virtual bool Dispatch(asizei setIndex, asizei resIndex) = 0;

	/*! CPU-side verification of a given header, already populated with the nonce to test.
	Hashing is performed coherently with the specified parameter set so for example a scrypt-N algorithm can discriminate
	between hashes for N=1024, N=2048 etc. This unfortunately mandates some replication of code, but that's it.
	\note Blocking call. */
	virtual void HashHeader(std::array<aubyte, 32> &hash, const std::array<aubyte, 128> &header, asizei setIndex, asizei resIndex) = 0;

	/*! Objects deriving from that are not necessarily copiable. As fact, due to hardware buffer management they are expected to be non-copiable.
	To support multiple threads however, all objects must be able to make partial copies of themselves so they are initialized with the unique configurations
	resulted by AddSettings (a configuration is a unique set of options, which is a setting). Resources are not copied but they have to be regen'd by calling
	GenResources on the new object. It is expected the resulting state would be the same for both. In general, the whole state is copied but resources, 
	including device mappings. */
	virtual void MakeResourcelessCopy(std::unique_ptr<AbstractAlgoImplementation> &yours) const = 0;

	/*! Returns 0 if the given device is unused (not mapped to a configuration). Otherwise 1+config index. */
	virtual asizei GetDeviceUsedConfig(const typename MiningProcessorsProvider::Device &dev) const = 0;

	/*! Returns the index of the device used in the specific algo-implementation-instance as identified by MiningProcessorsProvider linear index. */
	virtual asizei GetDeviceIndex(asizei setting, asizei instance) const = 0;

	/*! \sa MinerInterface::GetBadConfigReasons */
	virtual std::vector<std::string> GetBadConfigReasons(const typename MiningProcessorsProvider::Platform &plat, const typename MiningProcessorsProvider::Device &dev) const {
		std::vector<std::string> reasons;
		std::unique_ptr<AbstractAlgoImplementation> dontMess;
		MakeResourcelessCopy(dontMess);
		dontMess->ChooseSettings(plat, dev, [&reasons](const char *desc) { reasons.push_back(std::string(desc)); });
		return reasons;
	}


protected:
	typedef std::function<void(const char*)> RejectReasonFunc;

	/*! Select the most fitting settings from the available set of options, return an index if matched or at least settings.size() if the device
	is not eligible for computing this algo (example: because it is a CPU device).
	As M8M tries to be educational (and thus informative) this can also take a function to call to collect the various reasons the device is not eligible
	to a certain configuration. */
	virtual asizei ChooseSettings(const typename MiningProcessorsProvider::Platform &plat, const typename MiningProcessorsProvider::Device &dev, RejectReasonFunc callback) = 0;

    /*! This function has two purposes.
    The bottom line is: for each setting, each algo is free to use a different number of steps or even different kernels at each step depending on options.
    If options are specified, this gets the option index used. If we want to do an "option-invariant" enumeration, then the option index >= GetNumSettings().
    So it is requested, but not required, this can receive an option setting. If not given, then assume we're enumerating kernels independantly of settings.
    For each source file, a specific kernel is to be used. Those two strings are returned as a pair. When the .first field of a returned pair is empty, it is
    assumed all kernels have been enumerated. */
	virtual std::pair<std::string, std::string> GetSourceFileAndKernel(asizei settingIndex, auint stepIndex) const = 0;

	/*! Algorithm implementation version is no more to be used "as is". Instead, I request each algo implementation to produce a string describing itself.
	That string is concatenated to algorithm version (which is not a "custom" value but rather common) and hashed in a very stupid way. 
    Note this is called with no specific set of options so it must be able to sign all possible settings. */
	std::string CustomVersioningStrings() const {
        auint index = 0;
        std::pair<std::string, std::string> load;
        std::vector< std::pair<std::string, std::string> > steps;
        while((load = GetSourceFileAndKernel(GetNumSettings(), index)).first.length()) {
            steps.push_back(std::move(load));
            index++;
        }
        std::string blob;
        std::vector<char> src;
        for(const auto &el : steps) {
            auto path("kernels/" + el.first);
            blob += GetSourceFromFile(src, path.c_str());
            blob += el.second;
        }
        return blob;
    }

    //! \return src.data()
    static const char* GetSourceFromFile(std::vector<char> &src, const std::string &name) {
	    std::ifstream disk(name.c_str(), std::ios_base::binary);
	    if(disk.good() == false) throw std::string("File \"") + name + "\" cannot be opened for read.";
	    disk.seekg(0, std::ios_base::end);
	    aulong size = disk.tellg();
	    const asizei maxSize = 1024 * 1024 * 8;
	    if(size > maxSize) throw std::string("File \"") + name + "\" is way too big: " + std::to_string(size) + "B, max is " + std::to_string(maxSize) + "B.";
	    disk.seekg(0, std::ios_base::beg);
	    // CL2 is clear on the fact the strings don't need to be NULL-terminated. CL1.2 was rather clear in rationale as well, but it looks like
	    // some AMD APP versions really want this string NULL-terminated.
        src.resize(size + 1);
	    disk.read(src.data(), size);
	    src[size] = 0;
        return src.data();
    }
};