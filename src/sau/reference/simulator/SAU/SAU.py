# from sauunit import txt2ram
# 123
from dataclasses import dataclass, field, fields
from typing import List

import logging
from logging.handlers import RotatingFileHandler
import os
import numpy as np


LOG_MAX_BYTES = 2 * 1024 * 1024
LOG_BACKUP_COUNT = 8


@dataclass
class CSR:
	# csr 寄存器信息
	ins1_msb: int = 0
	ins1_lsb: int = 0
	ins2_msb: int = 0
	ins2_lsb: int = 0
	ins3_msb: int = 0
	ins3_lsb: int = 0
	ins4_msb: int = 0
	ins4_lsb: int = 0

	def reset(self):
		for base_field in fields(self):
			setattr(self, base_field.name,base_field.default)


@dataclass
class CSRaddr:
	# csr 寄存器地址
	INS1_LSB: int = 0x200
	INS1_MSB: int = 0x201
	INS2_LSB: int = 0x202
	INS2_MSB: int = 0x203
	INS3_LSB: int = 0x204
	INS3_MSB: int = 0x205
	INS4_LSB: int = 0x206
	INS4_MSB: int = 0x207


@dataclass
class ConfigReg:
	"""
	A configuration register to controll the VEU loop.
	"""
	A_address: 			int = 0
	A_address_xstep: 	int = 0
	A_address_chstep: 	int = 0
	B_address: 			int = 0
	B_address_xstep: 	int = 0
	B_address_chstep: 	int = 0
	D_address: 			int = 0
	D_address_xstep: 	int = 0
	D_address_chstep: 	int = 0
	flow_loop_times: 	int = 0
	work_mode: 			int = 0
	flow_mode: 			int = 0
	register_mode: 		int = 0
	conv_kernel: 		int = 0
	transpose_mode: 	int = 0
	stride: 			int = 0
	shift_mode: 		int = 0  # 是否需要截位
	cutbit: 			int = 0  # 截位
	C_address: 			int = 0
	ins_id: 			int = 0
	start: 				int = 0


@dataclass
class StatusReg:
	"""
	A configuration register to controll the SAU loop.
	"""
	running: int = 0
	running_time: int = 0
	flow_i: int = 0
	flow_k: int = 0
	A_address: int = 0
	A_step: int = 0
	A_count: int = 0
	A_kernel: int = 0
	A_bytes: int = 0
	B_address: int = 0
	B_step: int = 0
	B_count: int = 0
	B_kernel: int = 0
	B_bytes: int = 0
	C_address: int = 0
	C_step: int = 0
	C_count: int = 0
	C_kernel: int = 0
	C_bytes: int = 0
	C_en: int = 0
	D_address: int = 0
	D_step: int = 0
	D_count: int = 0
	D_kernel: int = 0
	D_wstrb: List[int] = field(default_factory=list)


@dataclass
class MatrixBuffer:
	matrix: List[List[int]] = field(default_factory=lambda: [[0 for _ in range(16)] for _ in range(16)])

	def __post_init__(self):
		if self.matrix is None:
			self.matrix = [[0 for _ in range(16)] for _ in range(16)]


class SAU:
	def __init__(self, unit_id:int, unit_size:int, unit_debug=False):
		# base info
		self.unit_id = unit_id
		self.unit_size = unit_size
		self.unit_bank = 16
		self.unit_debug = unit_debug
		self.base_addr = 0x2000_0000
		# inline-csr reg
		self.csr = CSR()
		self.csr_addr = CSRaddr()
		self.config = ConfigReg()
		self.status = StatusReg()

		# inline-data buffer
		self.input_matrix1 = MatrixBuffer()  # 输入矩阵1
		self.input_matrix2 = MatrixBuffer()  # 输入矩阵2
		self.input_matrix3 = MatrixBuffer()  # 输入矩阵2
		self.input_register = MatrixBuffer(matrix=[[0 for i in range(10)] for j in range(24)])
		self.A_matrix = np.zeros((unit_size,unit_size), dtype=np.int32)
		self.B_matrix = np.zeros((unit_size,unit_size), dtype=np.int32)
		self.C_matrix = np.zeros(unit_size, dtype=np.int32)
		self.D_matrix = np.zeros((unit_size,unit_size), dtype=np.int32)
		self.D_matrix_deq = np.zeros((unit_size,unit_size), dtype=np.int32)
		self.D_matrix_tmp = np.zeros((unit_size,unit_size), dtype=np.int32)
		self.output_matrix = MatrixBuffer(matrix=[[0 for i in range(unit_size)] for j in range(unit_size)])  # 输出矩阵

		# calculation time
		self.output_time = 0
		self.total_time = 0

		# print init info
		self.init()
		

	def init(self):
		# 目前没有需要init的功能
		self.logger_sau = logging.getLogger("sau")
		self.logger_sau.setLevel(logging.INFO if self.unit_debug else logging.WARNING)
		self.memory_log_seq = 0
		self.memory_summary_stats = {}
		self.memory_summary_total = 0
		if not self.logger_sau.handlers:
			os.makedirs('log', exist_ok=True)
			fh = RotatingFileHandler(
				'log/sau_running.log',
				mode='w',
				maxBytes=LOG_MAX_BYTES,
				backupCount=LOG_BACKUP_COUNT,
				encoding='utf-8',
			)
			fh.setFormatter(logging.Formatter('[%(levelname)s] : %(message)s (line:%(lineno)d [%(filename)s]'))
			self.logger_sau.addHandler(fh)
			self.logger_sau.propagate = False
		# log初始化信息
		self.logger_sau.debug("🐱--SAU Unit has been initialized.")
		self.logger_sau.debug(f"🐱--SAU Unit ID is :{self.unit_id}")


	def get_csr(self, csr_addr):
		"""
		Reads the current CSR state.
		This method retrieves the current state of the Control and Status Register (CSR).
		"""
		csr_map = {
			self.csr_addr.INS1_MSB: self.csr.ins1_msb,
			self.csr_addr.INS1_LSB: self.csr.ins1_lsb,
			self.csr_addr.INS2_MSB: self.csr.ins2_msb,
			self.csr_addr.INS2_LSB: self.csr.ins2_lsb,
			self.csr_addr.INS3_MSB: self.csr.ins3_msb,
			self.csr_addr.INS3_LSB: self.csr.ins3_lsb,
			self.csr_addr.INS4_MSB: self.csr.ins4_msb,
			self.csr_addr.INS4_LSB: self.csr.ins4_lsb

		}
		return csr_map.get(csr_addr, None)

	def set_csr(self, csr_en, operation, csr_addr, csr_wdata1, csr_wdata2):
		"""
		Writes to the CSR.
		This method updates the Control and Status Register (CSR) with new values.
		"""
		csr_map = {
			self.csr_addr.INS1_MSB: 'ins1_msb',
			self.csr_addr.INS1_LSB: 'ins1_lsb',
			self.csr_addr.INS2_MSB: 'ins2_msb',
			self.csr_addr.INS2_LSB: 'ins2_lsb',
			self.csr_addr.INS3_MSB: 'ins3_msb',
			self.csr_addr.INS3_LSB: 'ins3_lsb',
			self.csr_addr.INS4_MSB: 'ins4_msb',
			self.csr_addr.INS4_LSB: 'ins4_lsb'
		}
		# 指令解码过程放在这里 (wdata1是低位  wdata2是高位) 分了4种模式
		if csr_en and operation == 1:
			# 通过map写入
			csr_field1 = csr_map.get(csr_addr, None)
			csr_field2 = csr_map.get(csr_addr + 1, None)
			if csr_field1 is not None and csr_field2 is not None:
				setattr(self.csr, csr_field1, csr_wdata1)
				setattr(self.csr, csr_field2, csr_wdata2)
			if csr_addr == self.csr_addr.INS1_LSB:
				self.update_ins1_config()
			if csr_addr == self.csr_addr.INS4_LSB:
				self.update_csr()
		else:
			pass

	def update_ins1_config(self):
		ins1_lsb = self.csr.ins1_lsb
		ins1_msb = self.csr.ins1_msb
		if (ins1_lsb & ~0x3F) != 0 and (ins1_msb & ~0x3F) == 0:
			ins1_lsb, ins1_msb = ins1_msb, ins1_lsb
		self.config.transpose_mode = ins1_msb & 0x03
		self.config.cutbit = (ins1_msb >> 2) & 0x1F
		self.config.register_mode = ins1_lsb & 0x03
		self.config.conv_kernel = (ins1_lsb >> 2) & 0x03
		self.config.stride = (ins1_lsb >> 4) & 0x01
		self.config.shift_mode = (ins1_lsb >> 5) & 0x01

	def update_csr(self):
		"""
		从csr信号中分析
		"""
		self.update_ins1_config()
		# ins3
		self.config.B_address_chstep = (self.csr.ins2_msb) & 0xFF
		self.config.A_address_chstep = (self.csr.ins2_msb >> 8) & 0xFF
		self.config.D_address_chstep = (self.csr.ins2_msb >> 16) & 0xFF
		# ins4
		self.config.B_address_xstep = (self.csr.ins2_lsb) & 0x7F
		self.config.A_address_xstep = (self.csr.ins2_lsb >> 8) & 0x7F
		self.config.D_address_xstep = (self.csr.ins2_lsb >> 16) & 0x7F
		# ins5
		self.config.A_address = (self.csr.ins3_msb) & 0xFFFFF
		# ins6
		self.config.B_address = (self.csr.ins3_lsb) & 0xFFFFF
		# ins7
		self.config.C_address = (self.csr.ins4_msb) & 0xFFFFF
		self.config.work_mode = (self.csr.ins4_msb >> 20) & 0x03
		self.config.flow_mode = (self.csr.ins4_msb >> 22) & 0x03
		
		self.config.flow_loop_times = (self.csr.ins4_msb >> 24) & 0x3F
		# ins8
		self.config.start = (self.csr.ins4_lsb) & 0x01
		self.config.ins_id = (self.csr.ins4_lsb >> 1) & 0xFF
		self.config.D_address = (self.csr.ins4_lsb >> 9) & 0xFFFFF
		
		# runing status
		self.status.running_time = 0
		self.status.running = self.config.start
		# 每次指令输出一次
		self.status.D_address = self.config.D_address + self.base_addr
		self.status.D_step = self.config.D_address_chstep * self.config.D_address_xstep * self.unit_size
		self.status.D_count = self.unit_size
		self.status.D_kernel = (self.config.shift_mode + 1)

	def update_status(self):
		if self.config.conv_kernel > 1:
			# Conv mode
			if self.config.register_mode == 0:
				# 普通卷积burst读取配置
				if self.config.shift_mode == 1:
					self.status.A_address = self.config.A_address + self.base_addr + self.status.flow_i * self.config.A_address_chstep * self.config.A_address_xstep * self.unit_size // (self.config.shift_mode + 1)
					self.status.B_address = self.config.B_address + self.base_addr + self.status.flow_i * self.config.B_address_chstep * self.config.B_address_xstep * self.unit_size // (self.config.shift_mode + 1)
				else:
					self.status.A_address = self.config.A_address + self.base_addr + self.status.flow_i * self.config.A_address_chstep * self.config.A_address_xstep * self.unit_size
					self.status.B_address = self.config.B_address + self.base_addr + self.status.flow_i * self.config.B_address_chstep * self.config.B_address_xstep * self.unit_size
				self.status.A_step = self.config.A_address_xstep * self.unit_size
				self.status.A_count = self.config.conv_kernel
				self.status.B_step = self.config.B_address_xstep * self.unit_size
				self.status.B_count = self.config.conv_kernel ** 2
				self.status.C_address = self.config.C_address + self.base_addr
				self.status.C_step = self.unit_size
				self.status.C_count = self.unit_size // 8 # C矩阵16 * int16，8bit时为32列，16bit时为16列
				self.status.C_en = 1

			elif self.config.register_mode == 2:
				# DW卷积burst读取配置
				self.status.A_address = self.config.A_address + self.base_addr + self.status.flow_i * (self.config.stride+1) * self.config.A_address_xstep * self.unit_size // (self.config.shift_mode + 1)
				self.status.A_step = self.config.A_address_xstep * self.unit_size
				self.status.A_count = self.config.conv_kernel
				self.status.B_address = self.config.B_address + self.base_addr + self.status.flow_i * self.config.B_address_chstep * self.config.B_address_xstep * self.unit_size
				self.status.B_step = self.config.B_address_xstep * self.unit_size
				self.status.B_count = self.config.conv_kernel ** 2
				self.status.C_address = self.config.C_address + self.base_addr
				self.status.C_step = self.unit_size
				self.status.C_count = self.unit_size // 8 # C矩阵16 * int16，8bit时为32列，16bit时为16列
				self.status.C_en = 1

			# burst-kernel单词行数
			self.status.A_kernel = 1 + ((self.config.stride + 1)<< self.config.shift_mode) + self.config.shift_mode
			self.status.B_kernel = 1
			self.status.C_kernel = 1
			self.status.A_bytes = self.unit_size
			self.status.B_bytes = self.unit_size
			self.status.C_bytes = self.unit_size
		elif self.config.conv_kernel == 1:
			# PW Conv mode
			if(self.config.shift_mode == 1):
				self.status.A_address = self.config.A_address + self.base_addr + self.status.flow_i * self.config.A_address_chstep * self.config.A_address_xstep * self.unit_size * self.unit_size // (self.config.shift_mode + 1)
				self.status.B_address = self.config.B_address + self.base_addr + self.status.flow_i * self.config.B_address_chstep * self.config.B_address_xstep * self.unit_size // (self.config.shift_mode + 1)
				self.status.C_address = self.config.C_address + self.base_addr
			else:
				self.status.A_address = self.config.A_address + self.base_addr + self.status.flow_i * self.config.A_address_chstep * self.config.A_address_xstep * self.unit_size * self.unit_size
				self.status.B_address = self.config.B_address + self.base_addr + self.status.flow_i * self.config.B_address_chstep * self.config.B_address_xstep * self.unit_size
				self.status.C_address = self.config.C_address + self.base_addr
			# read step 
			self.status.A_step = self.config.A_address_xstep * self.unit_size
			self.status.B_step = self.config.B_address_xstep * self.unit_size
			self.status.C_step = self.unit_size
			# read count 
			self.status.A_count = self.unit_size
			self.status.B_count = self.unit_size
			self.status.C_count = self.unit_size // 8 # C矩阵16 * int16，8bit时为32列，16bit时为16列
			# burst-kernel单次行数
			self.status.A_kernel = self.config.shift_mode + 1
			self.status.B_kernel = 1
			self.status.C_kernel = 1
			# bytes
			self.status.A_bytes = self.unit_size
			self.status.B_bytes = self.unit_size
			self.status.C_bytes = self.unit_size
		else:
			# GEMM mode
			if self.unit_size == 8:
				if self.config.A_address_chstep == 0:
					self.status.A_address = self.config.A_address + self.base_addr + self.status.flow_i * self.unit_size * (self.config.shift_mode + 1)
				else:
					self.status.A_address = self.config.A_address + self.base_addr + self.status.flow_i * self.config.A_address_chstep * self.config.A_address_xstep * self.unit_size
				self.status.B_address = self.config.B_address + self.base_addr + self.status.flow_i * self.config.B_address_chstep * self.config.B_address_xstep * self.unit_size
			elif self.unit_size == 16:
				if self.config.A_address_chstep == 0:
					self.status.A_address = self.config.A_address + self.base_addr + self.status.flow_i * self.unit_size
				else:
					self.status.A_address = self.config.A_address + self.base_addr + self.status.flow_i * self.config.A_address_chstep * self.config.A_address_xstep * self.unit_size
				self.status.B_address = self.config.B_address + self.base_addr + self.status.flow_i * self.config.B_address_chstep * self.config.B_address_xstep * self.unit_size // (self.config.shift_mode + 1)
			self.status.C_address = self.config.C_address + self.base_addr

			self.status.A_step = self.config.A_address_xstep * self.unit_size
			self.status.B_step = self.config.B_address_xstep * self.unit_size
			self.status.C_step = self.unit_size
			self.status.A_count = self.unit_size
			self.status.B_count = self.unit_size
			self.status.C_count = self.unit_size // 8 # C矩阵16 * int16，8bit时为32列，16bit时为16列
			# burst-kernel单次行数
			self.status.A_kernel = self.config.shift_mode + 1
			self.status.B_kernel = 1
			self.status.C_kernel = 1
			self.status.A_bytes = self.unit_size
			self.status.B_bytes = self.unit_size
			self.status.C_bytes = self.unit_size


		# 配置写出的wstrb
		cache_wstrb_list = []
		for i in range(self.unit_size * (self.config.shift_mode + 1)):
			cache_wstrb = 0
			write_address = self.status.D_address + i * self.config.D_address_chstep * self.config.D_address_xstep * self.unit_size
			if self.unit_size == 8:
				if self.config.shift_mode == 1:
					# shift_mode为1时，写出8x16bit，写使能全1
					cache_wstrb = 0xFFFF
				else:
					if write_address % 16 == 0:
						# 前半行
						cache_wstrb = 0xFF00
					elif write_address % 16 == 8:
						# 后半行
						cache_wstrb = 0x00FF
					else:
						raise ValueError(f"D address must be aligned to an 8-byte half row, got {hex(write_address)}")
			elif self.unit_size == 16:
				cache_wstrb = 0xFFFF  # 16位单位大小，写使能全1
			# 写使能添加成 burst count
			cache_wstrb_list.append(cache_wstrb)
		# 更新状态寄存器的写使能
		self.status.D_wstrb = cache_wstrb_list

	def _normalize_input_rows(self, input_matrix):
		if input_matrix is None:
			return []
		if isinstance(input_matrix, np.ndarray):
			input_matrix = input_matrix.tolist()
		return list(input_matrix)

	def _normalize_sram_row(self, input_matrix, row_index):
		if row_index >= len(input_matrix):
			return [0 for _ in range(self.unit_bank)]

		row = input_matrix[row_index]
		if isinstance(row, np.ndarray):
			row = row.tolist()
		elif isinstance(row, tuple):
			row = list(row)
		elif not isinstance(row, list):
			row = [row]

		return list(row)

	def _pad_unit_window(self, row):
		if len(row) < self.unit_size:
			row = row + [0 for _ in range(self.unit_size - len(row))]
		return row[:self.unit_size]

	def _select_unit_window(self, input_matrix, row_index, address, matrix_name):
		row = self._normalize_sram_row(input_matrix, row_index)
		if self.unit_size == 16:
			return self._pad_unit_window(row)
		if self.unit_size != 8:
			return self._pad_unit_window(row)
		if len(row) <= self.unit_size:
			return self._pad_unit_window(row)
		if len(row) < self.unit_bank:
			row = row + [0 for _ in range(self.unit_bank - len(row))]

		offset = address % self.unit_bank
		if offset == 0:
			return row[8:16]
		if offset == 8:
			return row[:8]
		raise ValueError(f"{matrix_name} address must be aligned to an 8-byte half row, got {hex(address)}")

	def update_input_m1(self, input_matrix):
		"""
		读取matrix1,m1会出现8bit和16bit的情况
		"""
		cache_matrix = self._normalize_input_rows(input_matrix)
		expected_rows = self.status.A_count * self.status.A_kernel
		self.input_matrix1.matrix = [[0 for _ in range(self.unit_size)] for _ in range(expected_rows)]
		# 根据unit_size进行处理
		if self.unit_size == 8:
			# 两层循环包含burst的count和kernel：count是burst的次数，kernel是一次burst的带宽个数
			for i in range(self.status.A_count):
				for j in range(self.status.A_kernel):
					address = self.status.A_address + i * self.status.A_step + j * self.unit_size
					s = i * self.status.A_kernel + j
					self.input_matrix1.matrix[s] = self._select_unit_window(cache_matrix, s, address, "A")
		elif self.unit_size == 16:
			for i in range(self.status.A_count):
				for j in range(self.status.A_kernel):
					address = self.status.A_address + i * self.status.A_step + j * self.unit_size
					s = i * self.status.A_kernel + j
					self.input_matrix1.matrix[s] = self._select_unit_window(cache_matrix, s, address, "A")
	
	def update_input_m2(self, input_matrix):
		"""
		读取matrix2
		"""
		cache_matrix = self._normalize_input_rows(input_matrix)
		expected_rows = self.status.B_count * self.status.B_kernel
		self.input_matrix2.matrix = [[0 for _ in range(self.unit_size)] for _ in range(expected_rows)]
		# 根据unit_size进行处理
		if self.unit_size == 8:
			for i in range(self.status.B_count):
				for j in range(self.status.B_kernel):
					address = self.status.B_address + i * self.status.B_step + j * self.unit_size
					s = i * self.status.B_kernel + j
					self.input_matrix2.matrix[s] = self._select_unit_window(cache_matrix, s, address, "B")
		elif self.unit_size == 16:
			for i in range(self.status.B_count):
				for j in range(self.status.B_kernel):
					address = self.status.B_address + i * self.status.B_step + j * self.unit_size
					s = i * self.status.B_kernel + j
					self.input_matrix2.matrix[s] = self._select_unit_window(cache_matrix, s, address, "B")

	def update_input_m3(self, input_matrix):
		"""
		读取matrix3,m3会出现8bit和16bit的情况
		"""
		self.input_matrix3.matrix = [0 for _ in range(self.unit_size)]
		flattened = []
		input_rows = self._normalize_input_rows(input_matrix)
		if self.unit_size > 8 and len(input_rows) > 1:
			input_rows = input_rows[::-1]
		for row in input_rows:
			if isinstance(row, np.ndarray):
				row = row.tolist()
			if isinstance(row, list):
				flattened.extend(row)
			else:
				flattened.append(row)

		for col_pair in range(0, min(len(flattened), self.unit_size * 2), 2):
			if col_pair + 1 < len(flattened):
				combined_value = (flattened[col_pair] << 8) | flattened[col_pair + 1]
				self.input_matrix3.matrix[col_pair // 2] = combined_value

	def conv_mat_shift(self):
		"""针对conv模式下的数据复用移位"""
		# 步骤1：每 stride 行合并成一行
		if self.config.shift_mode == 1:
			temp_A = np.array(self.input_matrix1.matrix, dtype=np.uint8)  # 矩阵化
			temp_A = np.ascontiguousarray(np.fliplr(temp_A))  # 大小端翻转并确保内存连续
			temp_A = temp_A.view(np.int16)  # 内存重映射
			cache_A_matrix = temp_A.reshape(temp_A.shape[0] // 2, -1)  # 重塑形状
			# cache_A_matrix = np.fliplr(np.array(self.input_matrix1.matrix, dtype=np.uint16).view(np.int16))
		elif self.config.shift_mode == 0:
			cache_A_matrix = np.fliplr(np.array(self.input_matrix1.matrix, dtype=np.uint8).view(np.int8))
		rows, cols = cache_A_matrix.shape
		stride = self.config.stride + 2
		# 将每S行合并成一行（水平拼接）
		merged_rows = rows // stride
		merged_matrix = np.zeros((merged_rows, cols * stride), dtype=cache_A_matrix.dtype)
		for i in range(merged_rows):
			start_row = i * stride
			end_row = start_row + stride
			# 将S行水平拼接
			merged_matrix[i] = cache_A_matrix[start_row:end_row].flatten()

		# 步骤2：执行左移操作
		# 对A_matrix进行处理：每一行复制n遍并向左移动n个数据
		n = self.config.conv_kernel
		# 使用NumPy数组操作，避免list中间处理
		# 1. 复制每一行n次
		expanded_matrix = np.repeat(merged_matrix, n, axis=0)
		# 2. 为每个副本创建不同的左移量
		rows, cols = expanded_matrix.shape
		shift_amounts = np.tile(np.arange(n), merged_matrix.shape[0])
		# 3. 使用向量化操作进行左移
		result_matrix = np.zeros_like(expanded_matrix)
		for i, shift in enumerate(shift_amounts):
			if shift == 0:
				result_matrix[i] = expanded_matrix[i]
			else:
				result_matrix[i] = np.concatenate([expanded_matrix[i][shift:], np.zeros(shift, dtype=expanded_matrix.dtype)])
		
		# 步骤3：取最左边的cache_A_matrix的列宽的矩阵
		original_cols = cache_A_matrix.shape[1]  # 原始cache_A_matrix的列数
		final_result = result_matrix[:, :original_cols*(stride-1):(stride-1)] # 根据 stride 来判断片选的步长
		return final_result

	def conv_kernel_mask(self):
		if self.config.register_mode == 0:
			result_matrix = np.fliplr(np.array(self.input_matrix2.matrix, dtype=np.uint8).view(np.int8))
		elif self.config.register_mode == 2:
			# DW卷积的mask
			cache_matrix = np.fliplr(np.array(self.input_matrix2.matrix, dtype=np.uint8).view(np.int8))
			# 根据 flow_i 选择列，其余列归零
			result_matrix = np.zeros_like(cache_matrix)
			# 确保 flow_i 在有效范围内
			cache_flow_i = self.status.flow_k // (self.config.shift_mode + 1)
			if 0 <= cache_flow_i < cache_matrix.shape[1]:
				# 保持第 flow_i 列的数值，其余列归零
				result_matrix[:, cache_flow_i] = cache_matrix[:, cache_flow_i]
		return result_matrix

			

	def preprocess(self):
		""" 
		数据预处理 (what are we doing?)
		1. 转换成numpy矩阵
		2. 读取buffer数据类型限制
		3. 内存行的 大小端转换
		4. 输入矩阵是否转置
		"""
		if self.config.conv_kernel > 1:
			# 步骤 1、2、3
			# conv_mat_shift 是针对
			self.A_matrix = self.conv_mat_shift()
			self.B_matrix = self.conv_kernel_mask()
			# 一维向量 -> 二维矩阵
			self.C_matrix = np.fliplr(np.array(self.input_matrix3.matrix, dtype=np.uint16).view(np.int16).reshape(1, -1))

			# 步骤 4
			if self.config.transpose_mode == 1:
				self.A_matrix = self.A_matrix.transpose()
			elif self.config.transpose_mode == 2:
				self.B_matrix = self.B_matrix.transpose()
			else:
				pass
		elif self.config.conv_kernel == 1:
			# 步骤 1、2、3
			# view 慎用，因为会改变 numpy 中 array 的尺寸
			if self.config.shift_mode == 1:
				# A矩阵处理：矩阵化 -> 大小端翻转 -> 内存重映射 -> 重塑形状
				temp_A = np.array(self.input_matrix1.matrix, dtype=np.uint8)  # 矩阵化
				temp_A = np.ascontiguousarray(np.fliplr(temp_A))  # 大小端翻转并确保内存连续
				temp_A = temp_A.view(np.int16)  # 内存重映射
				self.A_matrix = temp_A.reshape(temp_A.shape[0] // 2, -1)  # 重塑形状
				
				self.B_matrix = np.fliplr(np.array(self.input_matrix2.matrix, dtype=np.uint8).view(np.int8))
				self.C_matrix = np.fliplr(np.array(self.input_matrix3.matrix, dtype=np.uint16).view(np.int16).reshape(1, -1))

			else:
				self.A_matrix = np.fliplr(np.array(self.input_matrix1.matrix, dtype=np.uint8).view(np.int8))
				self.B_matrix = np.fliplr(np.array(self.input_matrix2.matrix, dtype=np.uint8).view(np.int8))
				self.C_matrix = np.fliplr(np.array(self.input_matrix3.matrix, dtype=np.uint16).view(np.int16).reshape(1, -1))

			# 步骤 4 转置器
			if self.config.transpose_mode == 1:
				self.A_matrix = self.A_matrix.transpose()
			elif self.config.transpose_mode == 2:
				self.B_matrix = self.B_matrix.transpose()
			else:
				pass
		else:
			# 步骤 1、2、3
			# view 慎用，因为会改变 numpy 中 array 的尺寸
			if self.config.shift_mode == 1:
				temp_A = np.array(self.input_matrix1.matrix, dtype=np.uint8)  # 矩阵化
				temp_A = np.ascontiguousarray(np.fliplr(temp_A))  # 大小端翻转并确保内存连续
				temp_A = temp_A.view(np.int16)  # 内存重映射
				self.A_matrix = temp_A.reshape(temp_A.shape[0] // 2, -1)  # 重塑形状

				self.B_matrix = np.fliplr(np.array(self.input_matrix2.matrix, dtype=np.uint8).view(np.int8))
				self.C_matrix = np.fliplr(np.array(self.input_matrix3.matrix, dtype=np.uint16).view(np.int16).reshape(1, -1))

			else:
				self.A_matrix = np.fliplr(np.array(self.input_matrix1.matrix, dtype=np.uint8).view(np.int8))
				self.B_matrix = np.fliplr(np.array(self.input_matrix2.matrix, dtype=np.uint8).view(np.int8))
				self.C_matrix = np.fliplr(np.array(self.input_matrix3.matrix, dtype=np.uint16).view(np.int16).reshape(1, -1))

			# 步骤 4 转置器
			if self.config.transpose_mode == 1:
				self.A_matrix = self.A_matrix.transpose()
			elif self.config.transpose_mode == 2:
				self.B_matrix = self.B_matrix.transpose()
			else:
				pass

	def systolic_array(self):
		"""脉动处理  粗粒度仿真"""
		# 适配硬件设计，对水平输入矩阵进行转置
		self.A_matrix = self.A_matrix.transpose()
		# 根据 work_mode 区分脉动计算模式
		if self.config.work_mode == 0 or self.config.work_mode == 1 or self.config.work_mode == 2:
			# 矩阵乘法
			# if(self.config.shift_mode == 1):
			# 	# 先扩展到 int16
			# 	A16 = self.A_matrix.astype(np.int16)

			# 	A16[:, 1::2] = A16[:, 1::2] << 8
			# 	self.D_matrix_tmp = np.matmul(A16, self.B_matrix, dtype=np.int32)
			# else:
			# 	self.D_matrix_tmp = np.matmul(self.A_matrix, self.B_matrix, dtype=np.int32)
			self.D_matrix_tmp = np.matmul(self.A_matrix, self.B_matrix, dtype=np.int32)
		elif self.config.work_mode == 3:
			# 矩阵加法
			self.D_matrix_tmp = np.add(self.A_matrix, self.B_matrix, dtype=np.int32)

	def accumulate_array(self):
		# flow 间累加
		if self.config.flow_loop_times - self.status.flow_i:
			# accumulate
			self.D_matrix = self.D_matrix + self.D_matrix_tmp
		else:
			pass

	def c_plus(self):
		# 将C_matrix扩展成(unit_size, unit_size)格式，通过复制16行
		if self.C_matrix.ndim == 1:
			# 如果是1维数组，先转换为2维
			self.C_matrix = self.C_matrix.reshape(1, -1)
		
		# 复制第一行到unit_size行
		expanded_C_matrix = np.tile(self.C_matrix[0], (self.unit_size, 1)).astype(np.int32, copy=False)
		
		# Conv mode treats bias as output-scale data, so align it to the accumulator scale first.
		if self.config.work_mode == 2:
			expanded_C_matrix = expanded_C_matrix << self.config.cutbit

		if self.config.work_mode in {1, 2}:
			self.D_matrix = self.D_matrix + expanded_C_matrix
		else:
			pass

	def de_quant(self):
		""" 24bit ->> 8bit or 16bit """
		# 截位
		cutbit = self.config.cutbit
		if cutbit == 0 and (self.csr.ins1_lsb & 0x80):
			cutbit = (self.csr.ins1_lsb >> 2) & 0x1F
		for i in range(self.unit_size):
			for j in range(self.unit_size):
				value = self.D_matrix[i][j] >> cutbit
				# 饱和处理
				if self.config.shift_mode == 0:
					# int8 输出，饱和到 [-128, 127]
					self.D_matrix_deq[i][j] = np.clip(value, -128, 127)
				else:
					# int16 输出，饱和到 [-32768, 32767]
					self.D_matrix_deq[i][j] = np.clip(value, -32768, 32767)

	def transpose_output(self):
		""" transpose the output matrix """
		# 判断flow_mode 决定是否需要转置输出
		if (self.config.flow_mode == 0) or (self.config.flow_mode == 2):
			self.D_matrix_deq= self.D_matrix_deq
		elif (self.config.flow_mode == 1) or (self.config.flow_mode == 3):
			self.D_matrix_deq = self.D_matrix_deq.transpose()
		else:
			self.logger_sau.debug(f"Invalid flow mode: {self.config.flow_mode}")
	
	def update_output(self):
		""" reserved matrix between instructions """

		# 1、按输出位宽更新输出list,送往sram, 要进行内存大小端转换
		if self.unit_size == 8:
			if self.config.shift_mode == 0:
				# int8 输出
				cache_matrix = np.fliplr(self.D_matrix_deq.copy().astype(np.uint8))
				cache_matrix = np.tile(cache_matrix, (1, 2))  # 列扩展一倍
				self.output_matrix.matrix = cache_matrix.tolist()

			elif self.config.shift_mode == 1:
				# int16 输出
				# 每一列的一个int16数，拆分成两个int8数
				cache_matrix = np.fliplr(self.D_matrix_deq.copy().astype(np.uint16))
				# 向量化拆分：使用位运算一次性处理整个矩阵
				low_bytes = cache_matrix & 0xFF  # 提取低8位
				high_bytes = (cache_matrix >> 8) & 0xFF  # 提取高8位
				# 使用stack和reshape重新排列
				expanded_matrix = np.stack([high_bytes, low_bytes], axis=2).reshape(self.unit_size, -1)
				
				self.output_matrix.matrix = expanded_matrix.tolist()

		elif self.unit_size == 16:
			if(self.config.shift_mode == 0):
				# int8输出
				cache_matrix = np.fliplr(self.D_matrix_deq.copy().astype(np.uint8))
				self.output_matrix.matrix = cache_matrix.tolist()
			else:
				# int16输出
				cache_matrix = np.fliplr(self.D_matrix_deq.copy().astype(np.uint16))
				# 每一列的一个int16数，拆分成两个int8数
				low_bytes = cache_matrix & 0xFF  # 提取低8位
				high_bytes = (cache_matrix >> 8) & 0xFF  # 提取高8位
				# 使用stack和reshape重新排列
				expanded_matrix = np.stack([high_bytes, low_bytes], axis=2).reshape(-1, self.unit_size)
				# 大小端的问题，奇偶行对调
				expanded_matrix = expanded_matrix.reshape(-1, 2, self.unit_size)[:, ::-1, :].reshape(-1, self.unit_size)
				# 矩阵序列化
				self.output_matrix.matrix = expanded_matrix.tolist()
		
		# 2、flow_mode 决定是否在计算后清零
		if (self.config.flow_mode == 0) or (self.config.flow_mode == 1):
			self.D_matrix.fill(0)  # 清空矩阵
		elif (self.config.flow_mode == 2) or (self.config.flow_mode == 3):
			self.D_matrix = self.D_matrix
		else:
			self.logger_sau.debug(f"Invalid flow mode: {self.config.flow_mode}")

	def accumulate_flow_times(self):
		"""累计flow次数"""
		if(self.config.shift_mode == 1):
			self.status.flow_i += 2
			self.status.flow_k += 2
		else:
			self.status.flow_i += 1
			self.status.flow_k += 1

		self.output_time += 1


	def run(self):
		"""
		一次 SAU 的 flow 运行
		"""
		# flow 数据预处理
		self.preprocess()

		# Systolic Array 处理
		self.systolic_array()

		# 累加
		self.accumulate_array()

		if self.unit_debug:
			self.log_sau_running_info()

		if (self.status.flow_i == (self.config.flow_loop_times - 1)) or (self.config.shift_mode == 1 and self.status.flow_i == (self.config.flow_loop_times - 2)):
			# 加偏置
			self.c_plus()
			# 截位
			self.de_quant()
			# 输出转置
			self.transpose_output()

			#  更新内部寄存器
			self.update_output()
			# 0、log记录内容
			self.log_sau_output_info("back update")
		
		# 累计flow次数
		self.accumulate_flow_times()

	def repair_backward(self):
		# running 循环的终止条件：
		if self.status.flow_i >= self.config.flow_loop_times:
			# total time calculation
			if self.config.register_mode == 0:
				if self.config.conv_kernel:
					# cov = flowtimes(14+3*stride) + 24 ,stride = 1或者 2
					self.total_time = 24 + (3 * self.config.stride + 14) * self.config.flow_loop_times

					# self.total_time = 32 + 24 * (self.config.flow_loop_times - 1) + 32
				elif self.config.conv_kernel == 0:
					self.total_time = (20 + self.config.flow_loop_times * (self.config.shift_mode + 1) * 21 + 14)
			elif self.config.register_mode == 2:
				# dwcov = 12*flowtimes + 11*stride + 21  stride = 1或者 2
				self.total_time = 21 + 11 * self.config.stride + 12 * self.config.flow_loop_times
			# 反向修正
			self.status.running = 0
			self.status.flow_i = 0
			if self.config.flow_mode in {0, 1} or self.config.register_mode == 0:
				self.status.flow_k = 0
			else:
				pass

			logging.debug("🚀SAU once flow running is over!\n\n")

	def _merge_access_ranges(self, access_ranges):
		if not access_ranges:
			return []

		sorted_ranges = sorted(access_ranges, key=lambda item: (item[0], item[1]))
		merged_ranges = [list(sorted_ranges[0])]
		for start_addr, end_addr in sorted_ranges[1:]:
			last_range = merged_ranges[-1]
			if start_addr <= last_range[1] + 1:
				last_range[1] = max(last_range[1], end_addr)
			else:
				merged_ranges.append([start_addr, end_addr])
		return [(start_addr, end_addr) for start_addr, end_addr in merged_ranges]

	def _format_access_ranges(self, access_ranges, max_ranges=8):
		if not access_ranges:
			return "none"

		merged_ranges = self._merge_access_ranges(access_ranges)
		range_preview = []
		for start_addr, end_addr in merged_ranges[:max_ranges]:
			range_preview.append(f"{hex(start_addr)}~{hex(end_addr)}")
		if len(merged_ranges) > max_ranges:
			range_preview.append(f"...(+{len(merged_ranges) - max_ranges} more spans)")

		overall_start = min(start_addr for start_addr, _ in access_ranges)
		overall_end = max(end_addr for _, end_addr in access_ranges)
		return (
			f"overall={hex(overall_start)}~{hex(overall_end)}, "
			f"accesses={len(access_ranges)}, merged_spans={len(merged_ranges)}, "
			f"detail=[{', '.join(range_preview)}]"
		)

	def _normalize_summary(self, access_summary, invalid_summary):
		if invalid_summary is None:
			invalid_summary = {}

		normalized_access = {
			access_name: tuple(self._merge_access_ranges(access_ranges))
			for access_name, access_ranges in access_summary.items()
		}
		normalized_invalid = {
			access_name: tuple(self._merge_access_ranges(access_ranges))
			for access_name, access_ranges in invalid_summary.items()
		}
		return normalized_access, normalized_invalid

	def log_memory_access_summary(self, pc, access_summary, invalid_summary=None):
		if invalid_summary is None:
			invalid_summary = {}

		if not any(access_summary.values()) and not any(invalid_summary.values()):
			return

		normalized_access, normalized_invalid = self._normalize_summary(access_summary, invalid_summary)

		if not self.unit_debug:
			signature = (
				pc,
				repr(self.config),
				repr(self.status),
				tuple((access_name, normalized_access.get(access_name, ())) for access_name in sorted(normalized_access.keys())),
				tuple((access_name, normalized_invalid.get(access_name, ())) for access_name in sorted(normalized_invalid.keys())),
			)
			stats = self.memory_summary_stats.get(signature)
			if stats is None:
				stats = {
					"count": 0,
					"pc": pc,
					"config": repr(self.config),
					"status": repr(self.status),
					"access_summary": normalized_access,
					"invalid_summary": normalized_invalid,
				}
				self.memory_summary_stats[signature] = stats
			stats["count"] += 1
			self.memory_summary_total += 1
			return

		self.memory_log_seq += 1
		self.logger_sau.warning(f"================ SAU memory summary #{self.memory_log_seq} Start ======")
		self.logger_sau.warning(f"PC: {hex(pc)}, Config: {self.config}, Status: {self.status}")
		for access_name, access_ranges in normalized_access.items():
			self.logger_sau.warning(f"{access_name}: {self._format_access_ranges(access_ranges)}")
		for access_name, access_ranges in normalized_invalid.items():
			if access_ranges:
				self.logger_sau.warning(f"{access_name}_invalid: {self._format_access_ranges(access_ranges)}")
		self.logger_sau.warning(f"================ SAU memory summary #{self.memory_log_seq} End ========\n")

	def flush_memory_access_summary(self):
		if self.unit_debug or not self.memory_summary_stats:
			return

		self.logger_sau.warning("================ SAU memory aggregate Start ======")
		self.logger_sau.warning(
			f"total_loop_summaries={self.memory_summary_total}, unique_patterns={len(self.memory_summary_stats)}"
		)
		sorted_stats = sorted(
			self.memory_summary_stats.values(),
			key=lambda item: (-item["count"], item["pc"]),
		)
		for index, stats in enumerate(sorted_stats, start=1):
			self.logger_sau.warning(
				f"pattern #{index}: count={stats['count']}, pc={hex(stats['pc'])}, config={stats['config']}, status={stats['status']}"
			)
			for access_name, access_ranges in stats["access_summary"].items():
				if access_ranges:
					self.logger_sau.warning(f"{access_name}: {self._format_access_ranges(access_ranges)}")
			for access_name, access_ranges in stats["invalid_summary"].items():
				if access_ranges:
					self.logger_sau.warning(f"{access_name}_invalid: {self._format_access_ranges(access_ranges)}")
		self.logger_sau.warning("================ SAU memory aggregate End ========\n")

	
	def log_sau_running_info(self):
		"""
		记录SAU运行信息
		"""
		self.logger_sau.info(f"================ SAU once flow Start ======")
		self.logger_sau.info(f"SAU Unit ID: {self.unit_id}, Unit Size: {self.unit_size}")
		self.logger_sau.info(f"Configuration: {self.config}")
		self.logger_sau.info(f"Status: {self.status}")
		self.logger_sau.info(f"Output Time: {self.output_time}, Total Time: {self.total_time}")
		self.logger_sau.info(f"Input Matrix 1: \n {self.A_matrix}")
		self.logger_sau.info(f"Input Matrix 2: \n {self.B_matrix}")
		self.logger_sau.info(f"Input Matrix 3: \n {self.C_matrix}")
		self.logger_sau.info(f"Tile out Matrix: \n {self.D_matrix_tmp}")
		self.logger_sau.info(f"Accumulate Matrix: \n {self.D_matrix}")
		self.logger_sau.info(f"================ SAU once flow End ========\n")

	def log_sau_output_info(self, logtime):
		"""
		记录SAU运行信息
		"""
		self.logger_sau.info(f"================ SAU {logtime} output Start ======")
		self.logger_sau.info(f"Reserved Matrix: \n {self.D_matrix}")
		self.logger_sau.info(f"Dequant Matrix: \n {self.D_matrix_deq}")
		self.logger_sau.info(f"================ SAU {logtime} output End ========\n")


