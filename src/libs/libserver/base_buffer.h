#pragma once

// 追加大小
#if TestNetwork
#define ADDITIONAL_SIZE 10
#else
#define ADDITIONAL_SIZE 1024 * 128
#endif

// 最大缓冲
#define MAX_SIZE		1024 * 1024 // 1M

// 单个 TCP 包的总长上限（含长度头与协议头），超过则视为非法包直接断开连接。
// 协议里 TotalSizeType 是 unsigned short，天然上限就是 0xFFFF (64KB-1)，
// 所以这里必须使用 uint16 的上限，否则接收侧 `totalSize > MAX_PACKET_SIZE` 判断永远为 false，
// 上限校验形同虚设。
#define MAX_PACKET_SIZE		0xFFFF

class Buffer
{
public:
	virtual unsigned int GetEmptySize();
	void ReAllocBuffer(unsigned int dataLength);
	unsigned int GetEndIndex() const
	{
		return _endIndex;
	}

	unsigned int GetBeginIndex() const
	{
		return _beginIndex;
	}

	unsigned int GetTotalSize() const
	{
		return _bufferSize;
	}

protected:
	char* _buffer{ nullptr };
	unsigned int _beginIndex{ 0 }; // buffer数据 开始位与结束位
	unsigned int _endIndex{ 0 };

	unsigned int _bufferSize{ 0 }; // 总长度
};

