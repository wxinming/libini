#pragma once
#pragma execution_character_set("utf-8")

#include <Windows.h>
#include <QString>
#include <QVariant>
#include <QMutex>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>

/**
* @brief 扩展的QVariant类，支持JSON类型转换
* 该类继承自QVariant，增加了对QStringList、QJsonObject、QJsonArray和QByteArray的支持，
*/
class Variant : protected QVariant {
public:
	friend class Ini;

	enum class UserType {
		Range = QMetaType::User + 1,
	};

	// 构造函数
	inline Variant() : QVariant() {}
	inline ~Variant() {}
	inline Variant(int i) : QVariant(i) {}
	inline Variant(uint ui) : QVariant(ui) {}
	inline Variant(qlonglong ll) : QVariant(ll) {}
	inline Variant(qulonglong ull) : QVariant(ull) {}
	inline Variant(bool b) : QVariant(b) {}
	inline Variant(double d) : QVariant(d) {}
	inline Variant(float f) : QVariant(f) {}
	inline Variant(const char* s) : QVariant(s) {}
	inline Variant(const QString& s) : QVariant(s) {}
	inline Variant(const QStringList& strs) : QVariant(strs) {}
	inline Variant(const QLatin1String& s) : QVariant(s) {}
	inline Variant(const QJsonObject& jo) : QVariant(jo) {}
	inline Variant(const QJsonArray& ja) : QVariant(ja) {}
	inline Variant(const QByteArray& ba) : QVariant(ba) {}
	// 将会转换为x~y格式,使用toRange转换
	template<typename T, std::enable_if_t<std::is_arithmetic_v<T> ||
		std::is_same_v<T, QString>, int> = 0>
	inline Variant(const QPair<T, T>&p) : QVariant() {
		range_pair_ = p;
		d = QVariant::Private(static_cast<int>(UserType::Range));
	}

	// 基础类型转换
	using QVariant::toInt;
	using QVariant::toUInt;
	using QVariant::toLongLong;
	using QVariant::toULongLong;
	using QVariant::toBool;
	using QVariant::toDouble;
	using QVariant::toFloat;
	using QVariant::toString;

	// 扩展类型转换
	QStringList toStringList() const;
	QJsonObject toJsonObject() const;
	QJsonArray toJsonArray() const;
	QByteArray toByteArray() const;

	// 支持范围
	template<typename T, std::enable_if_t<std::is_arithmetic_v<T> ||
		std::is_same_v<T, QString>, int> = 0>
	inline QPair<T, T> toRange(bool* ok = nullptr) const {
		auto str = __super::toString();
		QPair<QVariant, QVariant> pair;
		if (!str.isEmpty()) {
			auto split = str.split("~", QString::SkipEmptyParts);
			if (split.size() == 2) {
				pair = qMakePair(split[0], split[1]);
			}

			if (ok) {
				*ok = split.size() == 2;
			}
		}
		else {
			if (ok) {
				*ok = false;
			}
			pair = range_pair_;
		}

		return { pair.first.value<T>(), pair.second.value<T>() };
	}

	// 类型信息查询
	using QVariant::type;
	using QVariant::typeName;
	using QVariant::canConvert;
	using QVariant::convert;
	using QVariant::isValid;
	using QVariant::isNull;
	using QVariant::userType;

private:
	QPair<QVariant, QVariant> range_pair_;
};

using IniTraverseArrayCb = ::std::function<bool(int index, const QString& key, const Variant& value)>;

class Ini
{
public:
	// RAII操作

	// 分组锁
	class GroupLocker {
	public:
		explicit GroupLocker(Ini* ini, const QString& groupName, bool isEndAll = false) : ini_(ini), is_end_all_(isEndAll) {
			ini_->beginGroup(groupName);
		}
		~GroupLocker() {
			is_end_all_ ? ini_->endAllGroup() : ini_->endGroup();
		}
	private:
		GroupLocker(const GroupLocker&) = delete;
		GroupLocker& operator=(const GroupLocker&) = delete;
		Ini* ini_;
		bool is_end_all_;
	};

	// 读数组锁
	class ReadArrayLocker {
	public:
		explicit ReadArrayLocker(Ini* ini, const QString& arrayName, int* arraySize) : ini_(ini) {
			Q_ASSERT(arraySize != nullptr);
			*arraySize = ini_->beginReadArray(arrayName);
		}
		~ReadArrayLocker() {
			ini_->endArray();
		}
	private:
		ReadArrayLocker(const ReadArrayLocker&) = delete;
		ReadArrayLocker& operator=(const ReadArrayLocker&) = delete;
		Ini* ini_;
	};

	// 写数组锁
	class WriteArrayLocker {
	public:
		explicit WriteArrayLocker(Ini* ini, const QString& arrayName, int arraySize = -1) : ini_(ini) {
			ini_->beginWriteArray(arrayName, arraySize);
		}
		~WriteArrayLocker() {
			ini_->endArray();
		}
	private:
		WriteArrayLocker(const WriteArrayLocker&) = delete;
		WriteArrayLocker& operator=(const WriteArrayLocker&) = delete;
		Ini* ini_;
	};

	/**
	 * @brief 构造函数
	 * @param[in] filePath INI文件路径，为空则创建内存INI
	 * @param[in] encryptData 是否启用数据加密
	 */
	explicit Ini(const QString& filePath = QString(), bool encryptData = false);

	/**
	 * @brief 析构函数
	 */
	~Ini();

	/*
	* @brief 拷贝构造函数
	*/
	Ini(const Ini& other);

	/*
	* @brief 赋值构造函数
	*/
	Ini& operator=(const Ini& other);

	// 删除移动构造
	Ini(Ini&&) = delete;

	// 删除赋值移动构造
	Ini& operator=(const Ini&&) = delete;

	/**
	 * @brief 获取当前INI文件路径
	 * @return INI文件路径
	 */
	QString filePath() const;

	//=====================================================================
	// 组操作
	//=====================================================================

	/**
	 * @brief 开始一个新的分组
	 * @param[in] prefix 分组前缀
	 *
	 * 后续的键值操作将在此分组下进行，可以嵌套调用以创建多级分组。
	 */
	void beginGroup(const QString& prefix);

	/**
	 * @brief 结束当前分组
	 *
	 * 返回到上一级分组上下文。
	 */
	void endGroup();

	/*
	* @brief 结束所有分组
	*/
	void endAllGroup();

	/**
	 * @brief 获取当前分组路径
	 * @return 当前分组路径
	 */
	QString group() const;

	//=====================================================================
	// 数组操作
	//=====================================================================

	/**
	 * @brief 开始读取数组
	 * @param[in] prefix 数组前缀
	 * @return 数组大小
	 *
	 * 用于读取现有数组，需要配合setArrayIndex和endArray使用。
	 */
	int beginReadArray(const QString& prefix);

	/**
	 * @brief 开始写入数组
	 * @param[in] prefix 数组前缀
	 * @param[in] size 数组大小，-1表示自动计算
	 *
	 * 用于写入新数组，需要配合setArrayIndex和endArray使用。
	 */
	void beginWriteArray(const QString& prefix, int size = -1);

	/**
	 * @brief 结束数组操作
	 *
	 * 完成数组读写操作，保存数组大小信息。
	 */
	void endArray();

	/**
	 * @brief 设置当前数组索引
	 * @param[in] i 数组索引
	 *
	 * 在beginReadArray或beginWriteArray之后调用，设置当前操作的数组元素索引。
	 */
	void setArrayIndex(int i);

	/*
	* @brief 遍历数组
	* @param[in] prefix 数组前缀
	* @param[in] func 回调函数(index, key, value)返回false停止遍历
	* @note 需要注意,此接口将会调用beginReadArray和endArray,如果已经调用了begin则会调用失败
	*/
	void traverseArray(const QString& prefix, IniTraverseArrayCb&& func);

	//=====================================================================
	// 键值操作
	//=====================================================================

	/**
	 * @brief 设置键值
	 * @param[in] key 键名
	 * @param[in] value 值
	 */
	void setValue(const QString& key, const Variant& value);

	/**
	 * @brief 获取键值
	 * @param[in] key 键名
	 * @param[in] defaultValue 默认值
	 * @return 键对应的值，若不存在则返回默认值
	 */
	Variant value(const QString& key, const Variant& defaultValue = Variant()) const;

	//=====================================================================
	// 注释操作
	//=====================================================================

	/**
	 * @brief 设置键的注释
	 * @param[in] key 键名
	 * @param[in] comment 注释内容
	 */
	void setComment(const QString& key, const QString& comment);

	/**
	 * @brief 获取键的注释
	 * @param[in] key 键名
	 * @param[in] defaultComment 默认注释
	 * @return 键对应的注释，若不存在则返回默认注释
	 */
	QString comment(const QString& key, const QString& defaultComment = QString()) const;

	//=====================================================================
	// 便利方法
	//=====================================================================

	/**
	 * @brief 设置键值并添加注释
	 * @param[in] key 键名
	 * @param[in] value 值
	 * @param[in] comment 注释内容
	 *
	 * 相当于连续调用setValue和setComment。
	 */
	void newValue(const QString& key, const Variant& value, const QString& comment = QString());

	//=====================================================================
	// 键管理
	//=====================================================================

	/**
	 * @brief 移除键
	 * @param[in] key 键名
	 */
	void remove(const QString& key);

	/**
	 * @brief 重命名键
	 * @param[in] oldKeyPath 旧键路径
	 * @param[in] newKeyName 新键名
	 *
	 * 示例:
	 * rename("config/key0", "key"); //第二个参数代表新名称, 将key0改为key, 不要写成config/key
	 */
	void rename(const QString& oldKeyPath, const QString& newKeyName);

	/**
	 * @brief 检查键是否存在
	 * @param[in] key 键名
	 * @return 存在返回true，否则返回false
	 */
	bool contains(const QString& key) const;

	/*
	* @brief 是否为分组
	* @param[in] key 键名
	* @return 是返回true, 否则返回false
	*/
	bool isGroup(const QString& key) const;

	/*
	* @brief 是否为数组
	* @param[in] key 键名
	* @return 是否返回true, 否则返回false
	*/
	bool isArray(const QString& key) const;

	//=====================================================================
	// 查询方法
	//=====================================================================

	/**
	 * @brief 获取所有键
	 * @return 包含所有键的字符串列表
	 */
	QStringList allKeys() const;

	/**
	 * @brief 获取当前分组的子键
	 * @return 包含当前分组所有子键的字符串列表
	 */
	QStringList childKeys() const;

	/**
	 * @brief 获取当前分组的子组
	 * @return 包含当前分组所有子组的字符串列表
	 */
	QStringList childGroups() const;

	/*
	* @brief 启用键排序
	* @param enable 是否启用
	*/
	void enableKeySort(bool enable = true);

	/*
	* @brief 上下文的数量
	* @return 上下文的数量
	*/
	int ctxCount() const;

protected:
	/**
	 * @brief 构建完整的组名和键名
	 * @param[in] key 输入键名
	 * @param[in] groupName 输出组名
	 * @param[in] keyName 输出键名
	 */
	void buildGroupAndKeyName(const QString& key, QString& groupName, QString& keyName) const;

	/**
	 * @brief 检查键是否存在的内部方法
	 * @param[in] key 键名
	 * @param[in] flag 检查标志
	 * @return 存在返回true，否则返回false
	 */
	bool contains(const QString& key, int flag) const;

	//=====================================================================
	// 加密相关
	//=====================================================================

	/**
	 * @brief Base64编码
	 * @param[in] data 待编码数据
	 * @param[in] length 数据长度
	 * @return 编码后的字符串
	 */
	std::string base64Encode(const uint8_t* data, size_t length) const;

	/**
	 * @brief Base64解码
	 * @param[in] encoded 待解码字符串
	 * @return 解码后的数据
	 */
	std::vector<uint8_t> base64Decode(const std::string& encoded) const;

	/**
	 * @brief 判断字符是否为Base64字符
	 * @param[in] c 待判断字符
	 * @return 是返回true，否则返回false
	 */
	bool isBase64(uchar c) const;

	/*
	* @brief 创建加密
	*/
	void createCrypt();

	/*
	* @brief 销毁加密
	*/
	void destroyCrypt();

	/*
	* @brief 加密数据
	* @param[in] data 需要加密的数据
	* @return 加密后的数据
	*/
	QString encryptData(const QString& data) const;

	/*
	* @brief 解密数据
	* @param[in] data 需要解密的数据
	* @return 解密后的数据
	*/
	QString decryptData(const QString& data) const;

	/*
	* @brief 获取分组子属性
	* @param[in] group 分组
	* @return 分组子属性
	*/
	QVector<QPair<QString, QString>> childProperties(const QString& group) const;

	// 上下文
	struct Ctx {
		QString group;
		QString arrayPrefix;
		int arrayIndex = -1;
		bool inArray = false;
		bool autoSize = true;
		int maxIndex = -1;
		bool writeArray = false;
	};

	/*
	* @brief 获取上下文
	* @return 上下文
	*/
	Ctx* ctx() const;

	/*
	* @brief 清空上下文
	* @note 如果在子线程中进行操作ini文件了,等待线程退出的时候,需要调用此函数来清理上下文
	* @note 只会清理子线程的上下文,主线程的不会被清理
	*/
	void clearCtx();

	/*
	* @brief 文件上锁
	*/
	void fileLock() const;

	/*
	* @brief 文件解锁
	*/
	void fileUnlock() const;

	/*
	* @brief 快速读取
	* @param[in] group 分组
	* @param[in] key 键(完整的键名)内部以\区分层级, 与QSettings一致
	* @param[in] defaultValue 默认值
	* @return 读取的值
	*/
	QString fastRead(const QString& group, const QString& key, const QString& defaultValue = QString()) const;

	/*
	* @brief 快速写入
	* @param[in] group 分组
	* @param[in] key 键(完整的键名)内部以\区分层级, 与QSettings一致
	* @param[in] value 写入的值
	*/
	void fastWrite(const QString& group, const QString& key, const QString& value) const;

	/*
	* @brief 快速删除
	* @param[in] group 分组
	* @param[in] key 键(完整的键名), 如果为空则删除group, 内部以\区分层级, 与QSettings一致
	*/
	void fastRemove(const QString& group, const QString& key) const;

	/*
	* @brief 快速重命名
	* @param[in] group 分组
	* @param[in] oldKeyPath 旧键路径(完整的键名)内部以\区分层级, 与QSettings一致
	* @param[in] newKeyName 新键名
	*/
	void fastRename(const QString& group, const QString& oldKeyPath, const QString& newKeyName);

private:
	/*
	* @brief 读取文件数据
	* @param[in] group 分组
	* @param[in] key 键
	* @param[in] defaultValue 默认值
	* @param[in] filePath 文件路径
	* @param[out] result 读取的结果
	* @return 返回读取到的值
	*/
	QString readFileData(const QString& group, const QString& key, const QString& defaultValue, const QString& filePath, bool* result = nullptr) const;

	/*
	* @brief 写入文件数据
	* @param[in] group 分组
	* @param[in] key 键
	* @param[in] value 值
	* @param[in] filePath 文件路径
	* @return 成功返回true, 失败返回false
	*/
	bool writeFileData(const QString& group, const QString& key, const QString& value, const QString& filePath) const;

	/*
	* @brief 移除文件数据
	* @param[in] group 分组
	* @param[in] key 键
	* @param[in] filePath 文件路径
	* @return 成功返回true, 失败返回false
	*/
	bool removeFileData(const QString& group, const QString& key, const QString& filePath) const;

	/*
	* @brief 包含文件数据
	* @param[in] group 分组
	* @param[in] key 键
	* @param[in] filePath 文件路径
	* @return 成功返回true, 失败返回false
	*/
	bool containsFileData(const QString& group, const QString& key, const QString& filePath) const;

private:
	//=====================================================================
	// 成员变量
	//=====================================================================
	mutable QMap<Qt::HANDLE, Ctx> ctx_map_;        // 线程上下文映射
	mutable QMutex ctx_mutex_;                     // 线程上下文互斥锁
	mutable QRecursiveMutex* recursive_mutex_;     // 递归互斥锁，保证线程安全
	QString ini_file_;                             // INI文件路径
	QString comment_file_;                         // INI注释文件路径
	bool encrypt_data_;                            // 是否加密数据标志
	bool key_sort_;                                // 是否键排序
	ULONG_PTR crypt_prov_ = 0;                     // 加密服务提供者句柄
	ULONG_PTR crypt_hash_ = 0;                     // 哈希对象句柄
	ULONG_PTR crypt_key_ = 0;                      // 加密密钥句柄
};

