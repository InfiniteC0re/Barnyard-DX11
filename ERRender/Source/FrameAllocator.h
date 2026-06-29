#pragma once
#include <Toshi/Toshi.h>

namespace remaster
{

template <class T, TSIZE MAX_SIZE>
class FrameAllocator
{
public:
	FrameAllocator() = default;
	~FrameAllocator() = default;

	T* Allocate()
	{
		if ( m_uiCurrentSize >= MAX_SIZE ) return TNULL;

		return TREINTERPRETCAST( T*, m_Buffer + ( sizeof( T ) * ( m_uiCurrentSize++ ) ) );
	}

	void Reset()
	{
		m_uiCurrentSize = 0;
	}

private:
	TCHAR m_Buffer[ sizeof( T ) * MAX_SIZE ];
	TSIZE m_uiCurrentSize;
};

} // namespace remaster

