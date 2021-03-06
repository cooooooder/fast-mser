#include "stdafx.h"

#include "mt_mat_cache.h"



namespace basicmath {
	mt_mat_cache s_mat_cache;
}

mt_auto_mat_cache::mt_auto_mat_cache() {
	s_mat_cache.enable_cache(sys_true);
}

mt_auto_mat_cache::~mt_auto_mat_cache() {
	s_mat_cache.enable_cache(sys_false);
}

mt_mat_cache::mt_mat_cache() {
	m_mutex = sys_thread_lock::create_mutex();
	m_memory_size = 1024 * 1024 * 1024;
	m_cache_number = 0;
}

mt_mat_cache::~mt_mat_cache() {
	sys_thread_lock::release_mutex(m_mutex);
}

void mt_mat_cache::set_memory_size(i64 memory_size) {
	{
		sys_thread_lock (s_mat_cache.m_mutex);
		s_mat_cache.m_memory_size = memory_size;

		s_mat_cache.statistic_memory();
	}
}

i64 mt_mat_cache::get_memory_size() {
	return s_mat_cache.m_memory_size;
}

void mt_mat_cache::output_cache_statistic() {
	basiclog_info2(L"m_get_number: "<<s_mat_cache.m_get_number<<L"m_cache_number: "<<s_mat_cache.m_cache_number);
}

void mt_mat_cache::enable_cache(b8 enable) {
	{
		sys_thread_lock (s_mat_cache.m_mutex);
		map<u64, thread_data>::iterator td = s_mat_cache.m_thread_data.find(sys_os::current_thread_id());

		if (td == s_mat_cache.m_thread_data.end()) {
			thread_data data;
			data.m_enable = enable;

			s_mat_cache.m_thread_data.insert(make_pair(sys_os::current_thread_id(), data));
		} else {
			td->second.m_enable = enable;
			s_mat_cache.statistic_memory();
		}
	}
}

mt_mat mt_mat_cache::get_as(const mt_mat& src) {
	return get(src.dim(), src.size(), src.depth_channel());
}

mt_mat mt_mat_cache::get(const vector<i32>& sizes, mt_Depth_Channel depth_channel) {
	basiclog_assert2(!sizes.empty());

	return get((i32)sizes.size(), &sizes[0], depth_channel);
}

mt_mat mt_mat_cache::get(i32 dim, const i32* sizes, mt_Depth_Channel depth_channel) {
	thread_data* data = NULL;
	
	{
		sys_thread_lock lock(m_mutex);

		++m_get_number;

		map<u64, thread_data>::iterator td = m_thread_data.find(sys_os::current_thread_id());

		if (td != m_thread_data.end()) {
			data = &td->second;
		}
	}

	if (data != NULL) {
		if (data->m_enable) {
			for (i32 i = 0; i < (int)m_caches.size(); ++i) {
				if (m_caches[i].reference_number() == 1 && m_caches[i].depth_channel() == depth_channel && m_caches[i].dim() == dim) {
					b8 same_size = sys_true;

					for (i32 j = 0; j < dim; ++j) {
						if (m_caches[i].size()[j] != sizes[j]) {
							same_size = sys_false;
						}
					}

					if (same_size) {
						m_caches[i].detach();
						m_cache_number += 1;
						return m_caches[i];
					}
				}
			}

			mt_mat new_mat;
			new_mat.create(dim, sizes, depth_channel);

			m_caches.push_back(new_mat);

			statistic_memory();

			return new_mat;
		} 
	}
		
	mt_mat new_mat;
	new_mat.create(dim, sizes, depth_channel);
	return new_mat;
}

void mt_mat_cache::statistic_memory() {
	i32 total_memory = 0;
	i32 used_memory = 0;

	for (i32 i = 0; i < (i32)m_caches.size(); ++i) {
		i32 cur_memory = m_caches[i].element_size() * m_caches[i].element_number();

		if (m_caches[i].reference_number() > 1) {
			used_memory += cur_memory;
		}

		total_memory += cur_memory;
	}

	if (total_memory > m_memory_size && total_memory > used_memory * 2) {
		basiclog_info2(sys_strcombine()<<L", begin release memory in mt_mat_cache, before total memory: "<<total_memory<<L", used memory: "<<used_memory);

		i32 retain_memory = used_memory * 2;

		vector<mt_mat>::iterator iter = m_caches.begin();

		while (iter != m_caches.end()) {
			if (iter->reference_number() == 1) {
				i32 cur_memory = iter->element_size() * iter->element_number();
				iter = m_caches.erase(iter);
				total_memory -= cur_memory;

				if (total_memory <= retain_memory) {
					break;
				}
			} else {
				++iter;
			}
		}

		basiclog_info2(sys_strcombine()<<L"finish release memory in mt_mat_cache, memory after releasing: "<<total_memory);
	}	
}