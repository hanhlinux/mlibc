
#include <string.h>
#include <errno.h>
#include <sys/auxv.h>

// for dup2()
#include <unistd.h>
// for open()
#include <fcntl.h>
// for tcgetattr()
#include <termios.h>
// for stat()
#include <sys/stat.h>

#include <mlibc/ensure.h>
#include <mlibc/cxx-support.hpp>
#include <mlibc/frigg-alloc.hpp>
#include <mlibc/posix-pipe.hpp>

#include <frigg/vector.hpp>
#include <frigg/hashmap.hpp>
#include <frigg/string.hpp>
#include <frigg/protobuf.hpp>

#include <posix.frigg_pb.hpp>
#include <fs.frigg_pb.hpp>

using FileMap = frigg::Hashmap<
	int,
	helx::Pipe,
	frigg::DefaultHasher<int>,
	MemoryAllocator
>;

FileMap &getFileMap() {
	static FileMap singleton(frigg::DefaultHasher<int>(), getAllocator());
	return singleton;
}

void __mlibc_initFs() {
	struct FileEntry {
		int fd;
		HelHandle pipe;
	};

	unsigned long openfiles;
	if(!peekauxval(AT_OPENFILES, &openfiles)) {
		for(auto entry = (FileEntry *)openfiles; entry->fd != -1; ++entry)
			getFileMap().insert(entry->fd, helx::Pipe(entry->pipe));
	}
}

int __mlibc_pushFd(HelHandle handle) {
	// TODO: limit the number of FDs?
	for(int fd = 0; ; fd++) {
		auto it = getFileMap().get(fd);
		if(it)
			continue;
		getFileMap().insert(fd, helx::Pipe(handle));
		return fd;
	}
}

int stat(const char *__restrict path, struct stat *__restrict result) {
	assert(!"Fix this");
	int fd = open(path, O_RDONLY);
	if(fd == -1) {
		__ensure(errno == ENOENT);
		return -1;
	}

	if(fstat(fd, result))
		__ensure("Could not fstat() internal file");

	if(close(fd))
		__ensure("Could not close() internal file");
	return 0;
}

int fstat(int fd, struct stat *result) {
	assert(!"Fix this");
	managarm::posix::ClientRequest<MemoryAllocator> request(getAllocator());
	request.set_request_type(managarm::posix::ClientRequestType::FSTAT);
	request.set_fd(fd);

	int64_t request_num = allocPosixRequest();
	frigg::String<MemoryAllocator> serialized(getAllocator());
	request.SerializeToString(&serialized);
	HelError error;
	posixPipe.sendStringReqSync(serialized.data(), serialized.size(),
			eventHub, request_num, 0, error);
	HEL_CHECK(error);

	int8_t buffer[128];
	size_t length;
	HelError response_error;
	posixPipe.recvStringRespSync(buffer, 128, eventHub, request_num, 0, response_error, length);
	HEL_CHECK(response_error);

	managarm::posix::ServerResponse<MemoryAllocator> response(getAllocator());
	response.ParseFromArray(buffer, length);
	if(response.error() == managarm::posix::Errors::SUCCESS) {
		memset(result, 0, sizeof(struct stat));
		result->st_dev = 1;
		result->st_ino = response.inode_num();
		result->st_mode = response.mode() | S_IFREG;
		result->st_nlink = response.num_links();
		result->st_uid = response.uid();
		result->st_gid = response.gid();
		result->st_rdev = 0;
		result->st_size = response.file_size();
		result->st_atim.tv_sec = response.atime_secs();
		result->st_atim.tv_nsec = response.atime_nanos();
		result->st_mtim.tv_sec = response.mtime_secs();
		result->st_mtim.tv_nsec = response.mtime_nanos();
		result->st_ctim.tv_sec = response.ctime_secs();
		result->st_ctim.tv_nsec = response.ctime_nanos();
		result->st_blksize = 4096;
		result->st_blocks = response.file_size() / 512 + 1;
		return 0;
	}else{
		__ensure(!"Unexpected error");
		__builtin_unreachable();
	}
}

int open(const char *path, int flags, ...) {
//	frigg::infoLogger.log() << "mlibc: open(\""
//			<< path << "\") called!" << frigg::EndLog();
	HelAction actions[4];
	HelEvent results[4];

	managarm::posix::ClientRequest<MemoryAllocator> req(getAllocator());
	req.set_request_type(managarm::posix::ClientRequestType::OPEN);
	req.set_path(frigg::String<MemoryAllocator>(getAllocator(), path));

	frigg::String<MemoryAllocator> ser(getAllocator());
	req.SerializeToString(&ser);
	uint8_t buffer[128];
	actions[0].type = kHelActionOffer;
	actions[0].flags = kHelItemAncillary;
	actions[1].type = kHelActionSendFromBuffer;
	actions[1].flags = kHelItemChain;
	actions[1].buffer = ser.data();
	actions[1].length = ser.size();
	actions[2].type = kHelActionRecvToBuffer;
	actions[2].flags = kHelItemChain;
	actions[2].buffer = buffer;
	actions[2].length = 128;
	actions[3].type = kHelActionPullDescriptor;
	actions[3].flags = 0;
	HEL_CHECK(helSubmitAsync(posixPipe.getHandle(), actions, 4, eventHub.getHandle(), 0));

	results[0] = eventHub.waitForEvent(0);
	results[1] = eventHub.waitForEvent(0);
	results[2] = eventHub.waitForEvent(0);
	results[3] = eventHub.waitForEvent(0);

	HEL_CHECK(results[0].error);
	HEL_CHECK(results[1].error);
	HEL_CHECK(results[2].error);
	HEL_CHECK(results[3].error);
	
	managarm::posix::ServerResponse<MemoryAllocator> resp(getAllocator());
	resp.ParseFromArray(buffer, results[2].length);
	if(resp.error() == managarm::posix::Errors::FILE_NOT_FOUND) {
		errno = ENOENT;
		return -1;
	}else{
		assert(resp.error() == managarm::posix::Errors::SUCCESS);
		return __mlibc_pushFd(results[3].handle);
	}
}

ssize_t read(int fd, void *buffer, size_t size){
	assert(!"Fix this");
//	frigg::infoLogger.log() << "read()" << frigg::EndLog();
	managarm::posix::ClientRequest<MemoryAllocator> request(getAllocator());
	request.set_request_type(managarm::posix::ClientRequestType::READ);
	request.set_fd(fd);
	request.set_size(size);

	int64_t request_num = allocPosixRequest();
	frigg::String<MemoryAllocator> serialized(getAllocator());
	request.SerializeToString(&serialized);
	HelError error;
	posixPipe.sendStringReqSync(serialized.data(), serialized.size(),
			eventHub, request_num, 0, error);
	HEL_CHECK(error);

	uint8_t msg_buffer[128];
	size_t length;
	HelError response_error;
	posixPipe.recvStringRespSync(msg_buffer, 128, eventHub,
			request_num, 0, response_error, length);
	HEL_CHECK(response_error);

	managarm::posix::ServerResponse<MemoryAllocator> response(getAllocator());
	response.ParseFromArray(msg_buffer, length);
	if(response.error() == managarm::posix::Errors::NO_SUCH_FD) {
		errno = EBADF;
		return -1;
	}else if(response.error() == managarm::posix::Errors::END_OF_FILE) {
		return 0;
	}
	assert(response.error() == managarm::posix::Errors::SUCCESS);
	
	size_t data_length;
	HelError data_error;
	posixPipe.recvStringRespSync(buffer, size, eventHub,
			request_num, 1, data_error, data_length);
	HEL_CHECK(data_error);
	return data_length;
};

ssize_t write(int fd, const void *data, size_t size) {
	HelAction actions[4];
	HelEvent results[4];

	auto file_it = getFileMap().get(fd);
	assert(file_it);

//	frigg::infoLogger.log() << "write()" << frigg::EndLog();
	managarm::fs::CntRequest<MemoryAllocator> req(getAllocator());
	req.set_req_type(managarm::fs::CntReqType::WRITE);
	req.set_fd(fd);

	frigg::String<MemoryAllocator> ser(getAllocator());
	req.SerializeToString(&ser);
	uint8_t buffer[128];
	actions[0].type = kHelActionOffer;
	actions[0].flags = kHelItemAncillary;
	actions[1].type = kHelActionSendFromBuffer;
	actions[1].flags = kHelItemChain;
	actions[1].buffer = ser.data();
	actions[1].length = ser.size();
	actions[2].type = kHelActionSendFromBuffer;
	actions[2].flags = kHelItemChain;
	actions[2].buffer = const_cast<void *>(data);
	actions[2].length = size;
	actions[3].type = kHelActionRecvToBuffer;
	actions[3].flags = 0;
	actions[3].buffer = buffer;
	actions[3].length = 128;
	HEL_CHECK(helSubmitAsync(file_it->getHandle(), actions, 4, eventHub.getHandle(), 0));

	results[0] = eventHub.waitForEvent(0);
	results[1] = eventHub.waitForEvent(0);
	results[2] = eventHub.waitForEvent(0);
	results[3] = eventHub.waitForEvent(0);

	HEL_CHECK(results[0].error);
	HEL_CHECK(results[1].error);
	HEL_CHECK(results[2].error);
	HEL_CHECK(results[3].error);

	managarm::fs::SvrResponse<MemoryAllocator> resp(getAllocator());
	resp.ParseFromArray(buffer, results[3].length);
	assert(resp.error() == managarm::fs::Errors::SUCCESS);

	// TODO: implement NO_SUCH_FD
/*	if(resp.error() == managarm::fs::Errors::NO_SUCH_FD) {
		errno = EBADF;
		return -1;
	}else*/ if(resp.error() == managarm::fs::Errors::SUCCESS) {
		//FIXME: handle partial writes
		return size;
	}else{
		__ensure(!"Unexpected error");
		__builtin_unreachable();
	}
}

off_t lseek(int fd, off_t offset, int whence) {
	assert(!"Fix this");
	managarm::posix::ClientRequest<MemoryAllocator> request(getAllocator());
	request.set_fd(fd);
	request.set_rel_offset(offset);

	if(whence == SEEK_SET) {
		request.set_request_type(managarm::posix::ClientRequestType::SEEK_ABS);
	}else if(whence == SEEK_CUR) {
		request.set_request_type(managarm::posix::ClientRequestType::SEEK_REL);
	}else if(whence == SEEK_END) {
		request.set_request_type(managarm::posix::ClientRequestType::SEEK_EOF);
	}else{
		frigg::panicLogger() << "Illegal whence argument" << frigg::endLog;
	}

	int64_t request_num = allocPosixRequest();
	frigg::String<MemoryAllocator> serialized(getAllocator());
	request.SerializeToString(&serialized);
	HelError error;
	posixPipe.sendStringReqSync(serialized.data(), serialized.size(),
			eventHub, request_num, 0, error);
	HEL_CHECK(error);

	uint8_t buffer[128];
	size_t length;
	HelError response_error;
	posixPipe.recvStringRespSync(buffer, 128, eventHub, request_num, 0, response_error, length);
	HEL_CHECK(response_error);

	managarm::posix::ServerResponse<MemoryAllocator> response(getAllocator());
	response.ParseFromArray(buffer, length);
	if(response.error() == managarm::posix::Errors::NO_SUCH_FD) {
		errno = EBADF;		
		return -1;
	}else if(response.error() == managarm::posix::Errors::SUCCESS) {
		return response.offset();
	}else{
		__ensure(!"Unexpected error");
		__builtin_unreachable();
	}
}

HelHandle __raw_map(int fd) {
	HelAction actions[4];
	HelEvent results[4];

	auto file_it = getFileMap().get(fd);
	assert(file_it);
	
	managarm::fs::CntRequest<MemoryAllocator> req(getAllocator());
	req.set_req_type(managarm::fs::CntReqType::MMAP);
	req.set_fd(fd);
	
	frigg::String<MemoryAllocator> ser(getAllocator());
	req.SerializeToString(&ser);
	uint8_t buffer[128];
	actions[0].type = kHelActionOffer;
	actions[0].flags = kHelItemAncillary;
	actions[1].type = kHelActionSendFromBuffer;
	actions[1].flags = kHelItemChain;
	actions[1].buffer = ser.data();
	actions[1].length = ser.size();
	actions[2].type = kHelActionRecvToBuffer;
	actions[2].flags = kHelItemChain;
	actions[2].buffer = buffer;
	actions[2].length = 128;
	actions[3].type = kHelActionPullDescriptor;
	actions[3].flags = 0;
	HEL_CHECK(helSubmitAsync(file_it->getHandle(), actions, 4, eventHub.getHandle(), 0));

	results[0] = eventHub.waitForEvent(0);
	results[1] = eventHub.waitForEvent(0);
	results[2] = eventHub.waitForEvent(0);
	results[3] = eventHub.waitForEvent(0);

	HEL_CHECK(results[0].error);
	HEL_CHECK(results[1].error);
	HEL_CHECK(results[2].error);
	HEL_CHECK(results[3].error);
	
	managarm::fs::SvrResponse<MemoryAllocator> resp(getAllocator());
	resp.ParseFromArray(buffer, results[2].length);
	assert(resp.error() == managarm::fs::Errors::SUCCESS);
	return results[3].handle;
}

int close(int fd) {
	assert(!"Fix this");
	managarm::posix::ClientRequest<MemoryAllocator> request(getAllocator());
	request.set_request_type(managarm::posix::ClientRequestType::CLOSE);
	request.set_fd(fd);

	int64_t request_num = allocPosixRequest();
	frigg::String<MemoryAllocator> serialized(getAllocator());
	request.SerializeToString(&serialized);
	HelError error;
	posixPipe.sendStringReqSync(serialized.data(), serialized.size(),
			eventHub, request_num, 0, error);
	HEL_CHECK(error);

	uint8_t buffer[128];
	size_t length;
	HelError response_error;
	posixPipe.recvStringRespSync(buffer, 128, eventHub, request_num, 0, response_error, length);
	HEL_CHECK(response_error);

	managarm::posix::ServerResponse<MemoryAllocator> response(getAllocator());
	response.ParseFromArray(buffer, length);
	if(response.error() == managarm::posix::Errors::NO_SUCH_FD) {
		errno = EBADF;		
		return -1;
	}else if(response.error() == managarm::posix::Errors::SUCCESS) {
		return 0;
	}else{
		__ensure(!"Unexpected error");
		__builtin_unreachable();
	}
}

int dup2(int src_fd, int dest_fd) {
	assert(!"Fix this");
	managarm::posix::ClientRequest<MemoryAllocator> request(getAllocator());
	request.set_request_type(managarm::posix::ClientRequestType::DUP2);
	request.set_fd(src_fd);
	request.set_newfd(dest_fd);

	int64_t request_num = allocPosixRequest();
	frigg::String<MemoryAllocator> serialized(getAllocator());
	request.SerializeToString(&serialized);
	HelError error;
	posixPipe.sendStringReqSync(serialized.data(), serialized.size(),
			eventHub, request_num, 0, error);
	HEL_CHECK(error);

	int8_t buffer[128];
	size_t length;
	HelError response_error;
	posixPipe.recvStringRespSync(buffer, 128, eventHub, request_num, 0, response_error, length);
	HEL_CHECK(response_error);

	managarm::posix::ServerResponse<MemoryAllocator> response(getAllocator());
	response.ParseFromArray(buffer, length);
	if(response.error() == managarm::posix::Errors::SUCCESS) {
		return dest_fd;
	}else if(response.error() ==  managarm::posix::Errors::NO_SUCH_FD) {
		errno = EBADF;
		return -1;
	}else {
		__ensure(!"Unexpected error");
		__builtin_unreachable();
	}
}

int fcntl(int, int, ...) {
	frigg::infoLogger() << "mlibc: Broken fcntl() called!" << frigg::endLog;
	return 0;
}

int isatty(int fd) {
	assert(!"Fix this");
	managarm::posix::ClientRequest<MemoryAllocator> request(getAllocator());
	request.set_request_type(managarm::posix::ClientRequestType::TTY_NAME);
	request.set_fd(fd);

	int64_t request_num = allocPosixRequest();
	frigg::String<MemoryAllocator> serialized(getAllocator());
	request.SerializeToString(&serialized);
	HelError error;
	posixPipe.sendStringReqSync(serialized.data(), serialized.size(),
			eventHub, request_num, 0, error);
	HEL_CHECK(error);

	int8_t buffer[128];
	size_t length;
	HelError response_error;
	posixPipe.recvStringRespSync(buffer, 128, eventHub, request_num, 0, response_error, length);
	HEL_CHECK(response_error);

	managarm::posix::ServerResponse<MemoryAllocator> response(getAllocator());
	response.ParseFromArray(buffer, length);
	if(response.error() == managarm::posix::Errors::SUCCESS) {
		return 1;
	}else if(response.error() ==  managarm::posix::Errors::BAD_FD) {
		errno = ENOTTY;
		return 0;
	}else {
		__ensure(!"Unexpected error");
		__builtin_unreachable();
	}
}

char *ttyname(int fd) {
	assert(!"Fix this");
	// TODO: this is not thread-safe.
	frigg::String<MemoryAllocator> cache(getAllocator());
	
	managarm::posix::ClientRequest<MemoryAllocator> request(getAllocator());
	request.set_request_type(managarm::posix::ClientRequestType::TTY_NAME);
	request.set_fd(fd);

	int64_t request_num = allocPosixRequest();
	frigg::String<MemoryAllocator> serialized(getAllocator());
	request.SerializeToString(&serialized);
	HelError error;
	posixPipe.sendStringReqSync(serialized.data(), serialized.size(),
			eventHub, request_num, 0, error);
	HEL_CHECK(error);

	int8_t buffer[128];
	size_t length;
	HelError response_error;
	posixPipe.recvStringRespSync(buffer, 128, eventHub, request_num, 0, response_error, length);
	HEL_CHECK(response_error);

	managarm::posix::ServerResponse<MemoryAllocator> response(getAllocator());
	response.ParseFromArray(buffer, length);
	if(response.error() == managarm::posix::Errors::SUCCESS) {
		cache = response.path();
		return cache.data();
	}else if(response.error() ==  managarm::posix::Errors::BAD_FD) {
		errno = ENOTTY;
		return nullptr;
	}else {
		__ensure(!"Unexpected error");
		__builtin_unreachable();
	}
}

int tcgetattr(int fd, struct termios *attr) {
	frigg::infoLogger() << "mlibc: Broken tcgetattr() called!" << frigg::endLog;
	attr->c_iflag = 0;
	attr->c_oflag = 0;
	attr->c_cflag = 0;
	attr->c_lflag = ECHO;
	for(size_t i = 0; i < NCCS; i++)
		attr->c_cc[i] = 0;
	attr->c_cc[VMIN] = 1;
	attr->c_cc[VTIME] = 0;
	return 0;
}

int tcsetattr(int, int, const struct termios *attr) {
	frigg::infoLogger() << "mlibc: Broken tcsetattr("
			<< (void *)attr->c_iflag << ", " << (void *)attr->c_oflag
			<< ", " << (void *)attr->c_cflag << ", " << (void *)attr->c_lflag
			<< ") called!" << frigg::endLog;
	return 0;
}

