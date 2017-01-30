#pragma once


template <typename T>
struct FreeList
{
	union FreeItem {
		FreeItem() {}
		char item[sizeof(T)];
		FreeItem* next;
	};

	struct ItemChunk {
		enum { Capacity = 8 };
		FreeItem items[Capacity];
	};

	T* alloc() {
		if (!nextFree) {
			ItemChunk& chunk = *new ItemChunk;
			nextFree = &chunk.items[0];
			for (int i = 0; i < ItemChunk::Capacity - 1; ++i) {
				chunk.items[i].next = &chunk.items[i + 1];
			}
			chunk.items[ItemChunk::Capacity - 1].next = nullptr;
		}

		char* res = &nextFree->item[0];
		nextFree = nextFree->next;
		return new(res) T;
	}

	void free(T* it) {
		FreeItem* item = reinterpret_cast<FreeItem*>(it);
		item->nextFree = nextFree;
		nextFree = item;
	}

private:
	FreeItem* nextFree = nullptr;
};
