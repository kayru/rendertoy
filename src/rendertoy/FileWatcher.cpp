#include "Common.h"
#include "FileWatcher.h"

#include <thread>
#include <string>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <cassert>

namespace FileWatcher {
	#define	MD5_BLOCK_LENGTH		64
	#define	MD5_DIGEST_LENGTH		16
	#define	MD5_DIGEST_STRING_LENGTH	(MD5_DIGEST_LENGTH * 2 + 1)

	struct MD5_CTX {
		u32	state[4];		/* state */
		u64	count;			/* number of bits, mod 2^64 */
		u8	buffer[MD5_BLOCK_LENGTH];	/* input buffer */
	};

	struct MD5Digest {
		u8 data[MD5_DIGEST_LENGTH];

		bool operator!=(const MD5Digest& rhs) const {
			return memcmp(data, rhs.data, sizeof(data)) != 0;
		}
	};

	void	 MD5Init(MD5_CTX*);
	void	 MD5Update(MD5_CTX *, const unsigned char *, size_t);
	void	 MD5Final(MD5Digest*, MD5_CTX*);


	vector<std::string>	watchedFiles;
	vector<MD5Digest>		fileDigests;
	vector<bool>			fileModifiedFlags;
	vector<Callback>		callbacks;

	vector<u32>			callbacksQueued;
	vector<u32>			callbacksDispatching;

	std::thread					watcherThread;
	bool						threadStopping = true;

	// This one is to sync the worker thread with the main thread(s)
	std::mutex					watcherMutex;

	// This one can't be used by the worker thread, and is used to make sure that only one
	// client thread at a time can access the API.
	std::mutex					publicApiMutex;

	bool calculateFileDigest(const std::string& path, MD5Digest *const res) {
		MD5_CTX ctx;
		MD5Init(&ctx);

		FILE* const f = fopen(path.c_str(), "r");
		if (f) {
			fseek(f, 0, SEEK_END);
			const u64 flen = ftell(f);
			fseek(f, 0, SEEK_SET);
			vector<u8> fileData(flen, u8(0));
			fread(fileData.data(), 1, flen, f);
			fclose(f);

			MD5Update(&ctx, fileData.data(), flen);
		}

		MD5Final(res, &ctx);
		return f != nullptr;
	}

	void watchFile(const char* const path, const Callback& callback) {
		MD5Digest digest;
		calculateFileDigest(path, &digest);

		publicApiMutex.lock();
		watcherMutex.lock();
			watchedFiles.push_back(path);
			fileDigests.push_back(digest);
			fileModifiedFlags.push_back(false);
			callbacks.push_back(callback);
		watcherMutex.unlock();
		publicApiMutex.unlock();
	}

	void stopWatchingFile(const char* const path) {
		publicApiMutex.lock();
		watcherMutex.lock();

		std::string pathStr = path;
		auto found = std::find(watchedFiles.begin(), watchedFiles.end(), pathStr);
		if (found != watchedFiles.end()) {
			const u32 idx = found - watchedFiles.begin();

			callbacksQueued.erase(
				std::remove(callbacksQueued.begin(), callbacksQueued.end(), idx),
				callbacksQueued.end()
			);

			watchedFiles.erase(watchedFiles.begin() + idx);
			fileDigests.erase(fileDigests.begin() + idx);
			fileModifiedFlags.erase(fileModifiedFlags.begin() + idx);
			callbacks.erase(callbacks.begin() + idx);
		}

		watcherMutex.unlock();
		publicApiMutex.unlock();
	}

	void threadFunc() {
		size_t fileIdxCounter = 0;

		while (!threadStopping) {
			if (watchedFiles.size() > 0) {
				watcherMutex.lock();

				const size_t i = fileIdxCounter++ % watchedFiles.size();

				if (!fileModifiedFlags[i]) {
					MD5Digest digest;
					if (calculateFileDigest(watchedFiles[i], &digest) && digest != fileDigests[i]) {
						fileModifiedFlags[i] = true;
						fileDigests[i] = digest;
						callbacksQueued.push_back(u32(i));
					}
				}

				watcherMutex.unlock();
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}

	void update() {
		publicApiMutex.lock();
			watcherMutex.lock();
				callbacksDispatching.swap(callbacksQueued);
			watcherMutex.unlock();

			for (u32 callbackIdx : callbacksDispatching) {
				callbacks[callbackIdx]();
				fileModifiedFlags[callbackIdx] = false;
			}

			callbacksDispatching.clear();
		publicApiMutex.unlock();
	}

	void start() {
		publicApiMutex.lock();
			assert(threadStopping);
			threadStopping = false;
			watcherThread = std::move(std::thread(&threadFunc));
		publicApiMutex.unlock();
	}

	void stop() {
		publicApiMutex.lock();
			threadStopping = true;
			watcherThread.join();
		publicApiMutex.unlock();
	}








#define PUT_64BIT_LE(cp, value) do {					\
	(cp)[7] = u8((value) >> 56);					\
	(cp)[6] = u8((value) >> 48);					\
	(cp)[5] = u8((value) >> 40);					\
	(cp)[4] = u8((value) >> 32);					\
	(cp)[3] = u8((value) >> 24);					\
	(cp)[2] = u8((value) >> 16);					\
	(cp)[1] = u8((value) >> 8);						\
	(cp)[0] = u8(value); } while (0)

#define PUT_32BIT_LE(cp, value) do {					\
	(cp)[3] = (value) >> 24;					\
	(cp)[2] = (value) >> 16;					\
	(cp)[1] = (value) >> 8;						\
	(cp)[0] = (value); } while (0)

	static u8 PADDING[MD5_BLOCK_LENGTH] = {
		0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};

	/* The four core functions - F1 is optimized somewhat */

	/* #define F1(x, y, z) (x & y | ~x & z) */
#define F1(x, y, z) (z ^ (x & (y ^ z)))
#define F2(x, y, z) F1(z, x, y)
#define F3(x, y, z) (x ^ y ^ z)
#define F4(x, y, z) (y ^ (x | ~z))

	/* This is the central step in the MD5 algorithm. */
#define MD5STEP(f, w, x, y, z, data, s) \
	( w += f(x, y, z) + data,  w = w<<s | w>>(32-s),  w += x )

	/*
	* The core of the MD5 algorithm, this alters an existing MD5 hash to
	* reflect the addition of 16 longwords of new data.  MD5Update blocks
	* the data and converts bytes into longwords for this routine.
	*/
	static void MD5Transform(u32 state[4], const u8 block[MD5_BLOCK_LENGTH])
	{
		u32 a, b, c, d, in[MD5_BLOCK_LENGTH / 4];

#ifndef WORDS_BIGENDIAN
		memcpy(in, block, sizeof(in));
#else
		for (a = 0; a < MD5_BLOCK_LENGTH / 4; a++) {
			in[a] = (u32)(
				(u32)(block[a * 4 + 0]) |
				(u32)(block[a * 4 + 1]) << 8 |
				(u32)(block[a * 4 + 2]) << 16 |
				(u32)(block[a * 4 + 3]) << 24);
		}
#endif

		a = state[0];
		b = state[1];
		c = state[2];
		d = state[3];

		MD5STEP(F1, a, b, c, d, in[0] + 0xd76aa478, 7);
		MD5STEP(F1, d, a, b, c, in[1] + 0xe8c7b756, 12);
		MD5STEP(F1, c, d, a, b, in[2] + 0x242070db, 17);
		MD5STEP(F1, b, c, d, a, in[3] + 0xc1bdceee, 22);
		MD5STEP(F1, a, b, c, d, in[4] + 0xf57c0faf, 7);
		MD5STEP(F1, d, a, b, c, in[5] + 0x4787c62a, 12);
		MD5STEP(F1, c, d, a, b, in[6] + 0xa8304613, 17);
		MD5STEP(F1, b, c, d, a, in[7] + 0xfd469501, 22);
		MD5STEP(F1, a, b, c, d, in[8] + 0x698098d8, 7);
		MD5STEP(F1, d, a, b, c, in[9] + 0x8b44f7af, 12);
		MD5STEP(F1, c, d, a, b, in[10] + 0xffff5bb1, 17);
		MD5STEP(F1, b, c, d, a, in[11] + 0x895cd7be, 22);
		MD5STEP(F1, a, b, c, d, in[12] + 0x6b901122, 7);
		MD5STEP(F1, d, a, b, c, in[13] + 0xfd987193, 12);
		MD5STEP(F1, c, d, a, b, in[14] + 0xa679438e, 17);
		MD5STEP(F1, b, c, d, a, in[15] + 0x49b40821, 22);

		MD5STEP(F2, a, b, c, d, in[1] + 0xf61e2562, 5);
		MD5STEP(F2, d, a, b, c, in[6] + 0xc040b340, 9);
		MD5STEP(F2, c, d, a, b, in[11] + 0x265e5a51, 14);
		MD5STEP(F2, b, c, d, a, in[0] + 0xe9b6c7aa, 20);
		MD5STEP(F2, a, b, c, d, in[5] + 0xd62f105d, 5);
		MD5STEP(F2, d, a, b, c, in[10] + 0x02441453, 9);
		MD5STEP(F2, c, d, a, b, in[15] + 0xd8a1e681, 14);
		MD5STEP(F2, b, c, d, a, in[4] + 0xe7d3fbc8, 20);
		MD5STEP(F2, a, b, c, d, in[9] + 0x21e1cde6, 5);
		MD5STEP(F2, d, a, b, c, in[14] + 0xc33707d6, 9);
		MD5STEP(F2, c, d, a, b, in[3] + 0xf4d50d87, 14);
		MD5STEP(F2, b, c, d, a, in[8] + 0x455a14ed, 20);
		MD5STEP(F2, a, b, c, d, in[13] + 0xa9e3e905, 5);
		MD5STEP(F2, d, a, b, c, in[2] + 0xfcefa3f8, 9);
		MD5STEP(F2, c, d, a, b, in[7] + 0x676f02d9, 14);
		MD5STEP(F2, b, c, d, a, in[12] + 0x8d2a4c8a, 20);

		MD5STEP(F3, a, b, c, d, in[5] + 0xfffa3942, 4);
		MD5STEP(F3, d, a, b, c, in[8] + 0x8771f681, 11);
		MD5STEP(F3, c, d, a, b, in[11] + 0x6d9d6122, 16);
		MD5STEP(F3, b, c, d, a, in[14] + 0xfde5380c, 23);
		MD5STEP(F3, a, b, c, d, in[1] + 0xa4beea44, 4);
		MD5STEP(F3, d, a, b, c, in[4] + 0x4bdecfa9, 11);
		MD5STEP(F3, c, d, a, b, in[7] + 0xf6bb4b60, 16);
		MD5STEP(F3, b, c, d, a, in[10] + 0xbebfbc70, 23);
		MD5STEP(F3, a, b, c, d, in[13] + 0x289b7ec6, 4);
		MD5STEP(F3, d, a, b, c, in[0] + 0xeaa127fa, 11);
		MD5STEP(F3, c, d, a, b, in[3] + 0xd4ef3085, 16);
		MD5STEP(F3, b, c, d, a, in[6] + 0x04881d05, 23);
		MD5STEP(F3, a, b, c, d, in[9] + 0xd9d4d039, 4);
		MD5STEP(F3, d, a, b, c, in[12] + 0xe6db99e5, 11);
		MD5STEP(F3, c, d, a, b, in[15] + 0x1fa27cf8, 16);
		MD5STEP(F3, b, c, d, a, in[2] + 0xc4ac5665, 23);

		MD5STEP(F4, a, b, c, d, in[0] + 0xf4292244, 6);
		MD5STEP(F4, d, a, b, c, in[7] + 0x432aff97, 10);
		MD5STEP(F4, c, d, a, b, in[14] + 0xab9423a7, 15);
		MD5STEP(F4, b, c, d, a, in[5] + 0xfc93a039, 21);
		MD5STEP(F4, a, b, c, d, in[12] + 0x655b59c3, 6);
		MD5STEP(F4, d, a, b, c, in[3] + 0x8f0ccc92, 10);
		MD5STEP(F4, c, d, a, b, in[10] + 0xffeff47d, 15);
		MD5STEP(F4, b, c, d, a, in[1] + 0x85845dd1, 21);
		MD5STEP(F4, a, b, c, d, in[8] + 0x6fa87e4f, 6);
		MD5STEP(F4, d, a, b, c, in[15] + 0xfe2ce6e0, 10);
		MD5STEP(F4, c, d, a, b, in[6] + 0xa3014314, 15);
		MD5STEP(F4, b, c, d, a, in[13] + 0x4e0811a1, 21);
		MD5STEP(F4, a, b, c, d, in[4] + 0xf7537e82, 6);
		MD5STEP(F4, d, a, b, c, in[11] + 0xbd3af235, 10);
		MD5STEP(F4, c, d, a, b, in[2] + 0x2ad7d2bb, 15);
		MD5STEP(F4, b, c, d, a, in[9] + 0xeb86d391, 21);

		state[0] += a;
		state[1] += b;
		state[2] += c;
		state[3] += d;
	}

	/*
	* Start MD5 accumulation.  Set bit count to 0 and buffer to mysterious
	* initialization constants.
	*/
	void MD5Init(MD5_CTX *ctx)
	{
		ctx->count = 0;
		ctx->state[0] = 0x67452301;
		ctx->state[1] = 0xefcdab89;
		ctx->state[2] = 0x98badcfe;
		ctx->state[3] = 0x10325476;
	}

	/*
	* Update context to reflect the concatenation of another buffer full
	* of bytes.
	*/
	void MD5Update(MD5_CTX *ctx, const unsigned char *input, size_t len)
	{
		size_t have, need;

		/* Check how many bytes we already have and how many more we need. */
		have = (size_t)((ctx->count >> 3) & (MD5_BLOCK_LENGTH - 1));
		need = MD5_BLOCK_LENGTH - have;

		/* Update bitcount */
		ctx->count += (u64)len << 3;

		if (len >= need) {
			if (have != 0) {
				memcpy(ctx->buffer + have, input, need);
				MD5Transform(ctx->state, ctx->buffer);
				input += need;
				len -= need;
				have = 0;
			}

			/* Process data in MD5_BLOCK_LENGTH-byte chunks. */
			while (len >= MD5_BLOCK_LENGTH) {
				MD5Transform(ctx->state, input);
				input += MD5_BLOCK_LENGTH;
				len -= MD5_BLOCK_LENGTH;
			}
		}

		/* Handle any remaining bytes of data. */
		if (len != 0)
			memcpy(ctx->buffer + have, input, len);
	}

	/*
	* Pad pad to 64-byte boundary with the bit pattern
	* 1 0* (64-bit count of bits processed, MSB-first)
	*/
	static void MD5Pad(MD5_CTX *ctx)
	{
		u8 count[8];
		size_t padlen;

		/* Convert count to 8 bytes in little endian order. */
		PUT_64BIT_LE(count, ctx->count);

		/* Pad out to 56 mod 64. */
		padlen = MD5_BLOCK_LENGTH -
			((ctx->count >> 3) & (MD5_BLOCK_LENGTH - 1));
		if (padlen < 1 + 8)
			padlen += MD5_BLOCK_LENGTH;
		MD5Update(ctx, PADDING, padlen - 8);		/* padlen - 8 <= 64 */
		MD5Update(ctx, count, 8);
	}

	/*
	* Final wrapup--call MD5Pad, fill in digest and zero out ctx.
	*/
	void MD5Final(MD5Digest* res, MD5_CTX *ctx)
	{
		int i;

		MD5Pad(ctx);
		for (i = 0; i < 4; i++)
			PUT_32BIT_LE(res->data + i * 4, ctx->state[i]);
		memset(ctx, 0, sizeof(*ctx));
	}
}
