#include "libini.h"
#include <wincrypt.h>
#pragma comment(lib, "advapi32.lib")
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QSet>
#include <QThread>
#include <mutex>

// 线程本地存储变量的定义
namespace ini {
	static std::mutex mutex;
	struct Context {
		inline Context()
			: mutex(nullptr)
		{
			counter = 0;
			mutex = std::make_unique<std::mutex>();
		}

		inline Context(const Context& ctx)
			: mutex(nullptr)
		{
			counter = ctx.counter;
			if (!mutex) {
				mutex = std::make_unique<std::mutex>();
			}
		}

		inline Context& operator=(const Context& ctx) {
			if (this == &ctx) {
				return *this;
			}
			counter = ctx.counter;
			if (!mutex) {
				mutex = std::make_unique<std::mutex>();
			}
			return *this;
		}

		inline void lock() { mutex->lock(); }
		inline void unlock() { mutex->unlock(); }

		int counter;
		std::unique_ptr<std::mutex> mutex;
	};
	static std::map<QString, Context> file_lock;
}

Ini::Ini(const QString& filePath, bool encryptData)
	:encrypt_data_(encryptData), recursive_mutex_(new QRecursiveMutex), key_sort_(false)
{
	//DBG_PRINT << __FUNCTION__;
	// 构造函数不需要加锁，因为对象还未被共享
	if (filePath.isEmpty()) {
		auto path = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/data";
		auto file = path + "/config.ini";
		ini_file_ = file;
		file = path + "/config-comment";
		comment_file_ = file;
	}
	else {
		QFileInfo fi(filePath);
		if (fi.isAbsolute()) {
			ini_file_ = filePath;
		}
		else {
			ini_file_ = fi.absoluteFilePath();
		}
		QFileInfo qfi(ini_file_);
		comment_file_ = (qfi.absolutePath() + "/" + qfi.baseName() + "-comment");
	}

	// 确保路径中都为/
	ini_file_.replace("\\", "/");
	auto dirPath = QFileInfo(ini_file_).path();
	if (!QDir(dirPath).exists()) {
		QDir dir;
		dir.mkpath(dirPath);
	}
	createCrypt();

	std::lock_guard<std::mutex> lock(ini::mutex);
	++ini::file_lock[ini_file_].counter;
	//DBG_PRINT << "add" << ini_file_ << "counter" << ini::file_lock[ini_file_].counter;
}

Ini::~Ini()
{
	destroyCrypt();
	if (recursive_mutex_) {
		delete recursive_mutex_;
		recursive_mutex_ = nullptr;
	}

	//DBG_PRINT << __FUNCTION__;
	std::lock_guard<std::mutex> lock(ini::mutex);
	if (--ini::file_lock[ini_file_].counter == 0) {
		ini::file_lock.erase(ini_file_);
		//DBG_PRINT << "remove" << ini_file_;
	}
}

Ini::Ini(const Ini& other)
	: recursive_mutex_(nullptr)
{
	if (!recursive_mutex_) {
		recursive_mutex_ = new QRecursiveMutex;
	}

	ini_file_ = other.ini_file_;
	comment_file_ = other.comment_file_;
	encrypt_data_ = other.encrypt_data_;
	key_sort_ = other.key_sort_;
	createCrypt();

	{
		std::lock_guard<std::mutex> lock(ini::mutex);
		++ini::file_lock[ini_file_].counter;
		//DBG_PRINT << "add" << ini_file_ << "counter" << ini::file_lock[ini_file_].counter;
	}
}

Ini& Ini::operator=(const Ini& other)
{
	if (this == &other) {
		return *this;
	}

	if (!recursive_mutex_) {
		recursive_mutex_ = new QRecursiveMutex;
	}

	{
		std::lock_guard<std::mutex> lock(ini::mutex);
		if (--ini::file_lock[ini_file_].counter == 0) {
			ini::file_lock.erase(ini_file_);
			//DBG_PRINT << "remove" << ini_file_;
		}
	}

	ini_file_ = other.ini_file_;
	comment_file_ = other.comment_file_;
	encrypt_data_ = other.encrypt_data_;
	key_sort_ = other.key_sort_;
	createCrypt();

	{
		std::lock_guard<std::mutex> lock(ini::mutex);
		++ini::file_lock[ini_file_].counter;
		//DBG_PRINT << "add" << ini_file_ << "counter" << ini::file_lock[ini_file_].counter;
	}
	return *this;
}

QString Ini::filePath() const
{
	QMutexLocker lock(recursive_mutex_);
	return ini_file_;
}

void Ini::beginGroup(const QString& prefix)
{
	// Q_ASSERT_X(ctx()->group.isEmpty(), __FUNCTION__, "in the same thread, ctx()->group is not empty, please call endGroup() to end it first!");
	// 不需要加锁，因为我们使用线程本地存储
	if (ctx()->group.isEmpty()) {
		ctx()->group = prefix;
	}
	else {
		ctx()->group += "/" + prefix;
	}
}

void Ini::endGroup()
{
	// 不需要加锁，因为我们使用线程本地存储
	auto lastSlash = ctx()->group.lastIndexOf('/');
	if (lastSlash != -1) {
		ctx()->group = ctx()->group.left(lastSlash);
	}
	else {
		ctx()->group = "";
		clearCtx();
	}
}

void Ini::endAllGroup()
{
	ctx()->group = "";
}

QString Ini::group() const
{
	// 不需要加锁，因为我们使用线程本地存储
	return ctx()->group;
}

int Ini::beginReadArray(const QString& prefix)
{
	//Q_ASSERT_X(ctx()->arrayPrefix.isEmpty(), __FUNCTION__, "in the same thread, ctx()->arrayPrefix is not empty, please call endArray() to end it first!");
	ctx()->arrayPrefix = prefix;
	ctx()->arrayIndex = -1;
	ctx()->inArray = true;
	ctx()->autoSize = true;
	ctx()->maxIndex = -1;
	ctx()->writeArray = false;

	return value("size", 0).toInt();
}

void Ini::beginWriteArray(const QString& prefix, int size)
{
	//Q_ASSERT_X(ctx()->arrayPrefix.isEmpty(), __FUNCTION__, "in the same thread, ctx()->arrayPrefix is not empty, please call endArray() to end it first!");
	ctx()->arrayPrefix = prefix;
	ctx()->arrayIndex = -1;
	ctx()->inArray = true;
	ctx()->maxIndex = -1;
	ctx()->writeArray = true;

	if (size != -1) {
		ctx()->autoSize = false;
		setValue("size", size);
	}
	else {
		ctx()->autoSize = true;
	}
}

void Ini::endArray()
{
	if (ctx()->autoSize && ctx()->writeArray) {
		setValue("size", ctx()->maxIndex != -1 ? ctx()->maxIndex + 1 : 0);
	}

	ctx()->inArray = false;
	ctx()->arrayPrefix = "";
	ctx()->arrayIndex = -1;
	ctx()->autoSize = true;
	ctx()->maxIndex = -1;
	ctx()->writeArray = false;

	if (ctx()->group.isEmpty()) {
		clearCtx();
	}
}

void Ini::setArrayIndex(int i)
{
	// 不需要加锁，因为我们使用线程本地存储
	ctx()->arrayIndex = i;
	if (ctx()->autoSize && i > ctx()->maxIndex) {
		ctx()->maxIndex = i;
	}
}

void Ini::traverseArray(const QString& prefix, IniTraverseArrayCb&& func)
{
	if (!ctx()->arrayPrefix.isEmpty()) {
		return;
	}

	int size = 0;
	Ini::ReadArrayLocker locker(this, prefix, &size);
	for (int i = 0; i < size; ++i) {
		setArrayIndex(i);
		auto keys = childKeys();
		for (int j = 0; j < keys.size(); ++j)
			if (!func(i, keys[j], value(keys[j])))
				return;
	}
}

// 修复后的辅助函数：构建组名和键名
void Ini::buildGroupAndKeyName(const QString& key, QString& groupName, QString& keyName) const
{
	if (ctx()->inArray && !ctx()->arrayPrefix.isEmpty()) {
		// 数组上下文处理（保持现有逻辑不变）
		if (key == "size") {
			// size键特殊处理：不使用数组索引
			if (!ctx()->group.isEmpty()) {
				groupName = ctx()->group;
				auto index = groupName.indexOf("/");
				if (index != -1) {
					auto temp0 = groupName.mid(0, index);
					auto temp1 = groupName.mid(index + 1);
					groupName = temp0;
					keyName = temp1 + "/" + ctx()->arrayPrefix + "/" + key;
				}
				else {
					keyName = ctx()->arrayPrefix + "/" + key;
				}
			}
			else {
				groupName = ctx()->arrayPrefix;
				auto firstSlash = groupName.indexOf("/");
				if (firstSlash != -1) {
					keyName = groupName.mid(firstSlash + 1) + "/" + key;
					groupName = groupName.mid(0, firstSlash);
				}
				else {
					keyName = key;
				}
			}
		}
		else if (ctx()->arrayIndex != -1) {
			// 在数组上下文中且设置了索引（普通数组元素）
			if (!ctx()->group.isEmpty()) {
				// 有组路径：group/array\index\key
				groupName = ctx()->group;
				auto index = groupName.indexOf("/");
				if (index != -1) {
					auto temp0 = groupName.mid(0, index);
					auto temp1 = groupName.mid(index + 1);
					groupName = temp0;
					keyName = temp1 + "/" + ctx()->arrayPrefix + "/" + QString::number(ctx()->arrayIndex + 1) + "/" + key;
				}
				else {
					keyName = ctx()->arrayPrefix + "/" + QString::number(ctx()->arrayIndex + 1) + "/" + key;
				}
			}
			else {
				// 无组路径：array组中的index\key
				groupName = ctx()->arrayPrefix;
				auto index = groupName.indexOf("/");
				if (index != -1) {
					keyName = groupName.mid(index + 1) + "/" + QString::number(ctx()->arrayIndex + 1) + "/" + key;
					groupName = groupName.left(index);
				}
				else {
					keyName = QString::number(ctx()->arrayIndex + 1) + "/" + key;
				}
			}
		}
		else {
			// 在数组上下文中但未设置索引（用于其他元数据）
			if (!ctx()->group.isEmpty()) {
				groupName = ctx()->group;
				keyName = ctx()->arrayPrefix + "/" + key;
			}
			else {
				groupName = ctx()->arrayPrefix;
				keyName = key;
			}
		}
	}
	else {
		// 非数组上下文处理

		// 解析键名中的路径
		auto firstSlash = key.indexOf('/');
		if (firstSlash != -1) {
			if (!ctx()->group.isEmpty()) {
				// 有组上下文：组合组路径和键路径
				//groupName = ctx()->group + "/" + keyPath;
				groupName = ctx()->group;
				keyName = key;

				auto firstSlash = groupName.indexOf("/");
				if (firstSlash != -1) {
					keyName = groupName.mid(firstSlash + 1) + "/" + keyName;
					groupName = groupName.left(firstSlash);
				}
			}
			else {
				// 键名包含路径分隔符
				QString keyPath = key.left(firstSlash);
				keyName = key.mid(firstSlash + 1);

				// 验证解析结果
				if (keyName.isEmpty()) {
					// 无效的键名（如 "config/"）
					groupName = "";
					return;
				}

				// 无组上下文：直接使用键路径作为组名
				groupName = keyPath;
			}
		}
		else {
			// 键名不包含路径分隔符
			keyName = key;
			if (!ctx()->group.isEmpty()) {
				groupName = ctx()->group;
				auto firstSlash = groupName.indexOf("/");
				if (firstSlash != -1) {
					keyName = groupName.mid(firstSlash + 1) + "/" + keyName;
					groupName = groupName.left(firstSlash);
				}
			}
			else {
				// 这种情况下没有组名
				// 保持为空，让调用者处理这种情况
				groupName = "";
			}
		}
	}

	if (key == groupName + "/" + keyName) {
		const_cast<Ini*>(this)->clearCtx();
	}
	//keyName.replace('/', '\\');
}

void Ini::setValue(const QString& key, const Variant& value)
{
	QMutexLocker locker(recursive_mutex_);

	QString groupName;
	QString keyName;
	buildGroupAndKeyName(key, groupName, keyName);

	if (groupName.isEmpty()) {
		groupName = "General";
		keyName = key;
	}

	QString str;
	if (value.type() == QVariant::Type::StringList) {
		auto strs = dynamic_cast<QVariant&>(const_cast<Variant&>(value)).toStringList();
		if (!strs.isEmpty()) {
			auto joined = "\"" + strs.join("\", \"") + "\"";
			str = "{" + joined + "}";
		}
		else {
			str = "{}";
		}
	}
	else if (value.type() == QMetaType::QJsonObject) {
		auto jsonobj = dynamic_cast<QVariant&>(const_cast<Variant&>(value)).toJsonObject();
		QJsonDocument doc(jsonobj);
		str = doc.toJson(QJsonDocument::Compact);
	}
	else if (value.type() == QMetaType::QJsonArray) {
		auto jsonarr = dynamic_cast<QVariant&>(const_cast<Variant&>(value)).toJsonArray();
		QJsonDocument doc(jsonarr);
		str = doc.toJson(QJsonDocument::Compact);
	}
	else if (value.type() == QVariant::ByteArray) {
		auto bytes = dynamic_cast<QVariant&>(const_cast<Variant&>(value)).toByteArray();
		QStringList strs;
		if (!bytes.isEmpty()) {
			for (int i = 0; i < bytes.size(); ++i) {
				auto uc = static_cast<uchar>(bytes[i]);
				strs.append(QString::asprintf("0x%02x", uc));
			}
			str = "{" + strs.join(",") + "}";
		}
		else {
			str = "{}";
		}
	}
	else if (value.userType() == static_cast<int>(Variant::UserType::Range)) {
		// QPair->range
		auto pair = value.toRange<QString>();
		str = QString("%1~%2").arg(pair.first, pair.second);
	}
	else {
		str = value.toString();
	}

	writeFileData(groupName, keyName, str, ini_file_);
}

void Ini::setComment(const QString& key, const QString& comment)
{
	QMutexLocker locker(recursive_mutex_);

	QString groupName;
	QString keyName;
	buildGroupAndKeyName(key, groupName, keyName);

	if (groupName.isEmpty()) {
		groupName = "General";
		keyName = key;
	}

	writeFileData(groupName, keyName, comment, comment_file_);
}

void Ini::newValue(const QString& key, const Variant& value, const QString& comment)
{
	// 这里不需要加锁，因为contains、setValue和setComment方法内部已经有锁
	if (!contains(key)) {
		setValue(key, value);
	}

	if (!comment.isEmpty()) {
		if (!contains(key, 1)) {
			setComment(key, comment);
		}
	}
}

Variant Ini::value(const QString& key, const Variant& defaultValue) const
{
	QMutexLocker locker(recursive_mutex_);

	QString groupName;
	QString keyName;
	buildGroupAndKeyName(key, groupName, keyName);

	if (groupName.isEmpty()) {
		groupName = "General";
		keyName = key;
	}

	return readFileData(groupName, keyName, defaultValue.toString(), ini_file_);
}

QString Ini::comment(const QString& key, const QString& defaultComment) const
{
	QMutexLocker locker(recursive_mutex_);

	QString groupName;
	QString keyName;
	buildGroupAndKeyName(key, groupName, keyName);

	if (groupName.isEmpty()) {
		groupName = "General";
		keyName = key;
	}

	return readFileData(groupName, keyName, defaultComment, comment_file_);
}

void Ini::remove(const QString& key)
{
	QMutexLocker locker(recursive_mutex_);

	QString groupName;
	QString keyName;
	buildGroupAndKeyName(key, groupName, keyName);

	if (groupName.isEmpty()) {
		// 删除整个group
		removeFileData(key, QString(), ini_file_);
		return;
	}

	if (isGroup(groupName + "/" + keyName)) {
		auto properties = childProperties(groupName);
		for (const auto& x : properties) {
			if (x.first.indexOf(keyName + "/") != -1) {
				removeFileData(groupName, x.first, ini_file_);
				removeFileData(groupName, x.first, comment_file_);
			}
		}
	}
	else if (isArray(groupName + "/" + keyName)) {
		auto properties = childProperties(groupName);
		auto size = 0;
		for (const auto& x : properties) {
			if (x.first == keyName + "/size") {
				size = x.second.toInt();
				break;
			}
		}

		QSet<QString> elements;
		auto index = 0;
		for (const auto& x : properties) {
			index = x.first.indexOf(keyName + "/1");
			if (index != -1) {
				index = x.first.lastIndexOf("/");
				if (index != -1) {
					elements.insert(x.first.mid(index + 1));
				}
			}
		}

		for (const auto& x : elements) {
			for (int i = 0; i < size; ++i) {
				auto tempKeyName = keyName + "/" + QString::number(i + 1) + "/" + x;
				removeFileData(groupName, tempKeyName, ini_file_);
				removeFileData(groupName, tempKeyName, comment_file_);
			}
		}

		removeFileData(groupName, keyName + "/size", ini_file_);
		removeFileData(groupName, keyName + "/size", comment_file_);
	}
	else {
		removeFileData(groupName, keyName, ini_file_);
		removeFileData(groupName, keyName, comment_file_);
	}
}

void Ini::rename(const QString& oldKeyPath, const QString& newKeyName)
{
	QMutexLocker locker(recursive_mutex_);

	QString groupName;
	QString keyName;
	buildGroupAndKeyName(oldKeyPath, groupName, keyName);

	if (groupName.isEmpty()) {
		// 则直接进行修改组名
		auto properties = childProperties(oldKeyPath);
		for (int i = 0; i < properties.size(); ++i) {
			const auto& pair = properties[i];
			const auto& key = pair.first;
			const auto& value = pair.second;
			setValue(QString("%1/%2").arg(newKeyName, key), value);
		}
		remove(oldKeyPath);
	}
	else {
		auto properties = childProperties(groupName);
		for (int i = 0; i < properties.size(); ++i) {
			auto pair = properties[i];
			if (pair.first.indexOf(keyName) != -1 &&
				pair.first.indexOf(keyName + "/") != -1) {
				auto result = false;
				auto value = readFileData(groupName, pair.first, QString(), ini_file_, &result);
				if (!result) {
					return;
				}

				auto lastSlash0 = keyName.lastIndexOf("/");
				auto lastSlash1 = pair.first.lastIndexOf("/") + 1;
				auto lastSlash2 = oldKeyPath.lastIndexOf("/");
				QString realOldKey;
				if (lastSlash2 != -1) {
					realOldKey = oldKeyPath.mid(lastSlash2 + 1);
				}
				else {
					realOldKey = oldKeyPath;
				}
				auto keyOld = keyName.mid(0, lastSlash0) + "/" + realOldKey + "/" + pair.first.mid(lastSlash1);
				auto keyNew = keyName.mid(0, lastSlash0) + "/" + newKeyName + "/" + pair.first.mid(lastSlash1);
				auto contains = false;
				for (int j = 0; j < properties.size(); ++j) {
					if (properties[j].first == keyNew) {
						contains = true;
						break;
					}
				}

				if (contains) {
					continue;
				}

				writeFileData(groupName, keyNew, value, ini_file_);
				auto description = readFileData(groupName, pair.first, QString(), comment_file_, &result);
				if (result) {
					writeFileData(groupName, keyNew, description, comment_file_);
				}
				fastRemove(groupName, keyOld);
			}
			else if (pair.first == keyName) {
				auto i0 = pair.first.lastIndexOf("/");
				if (i0 != -1) {
					auto keyNew = pair.first.mid(0, i0) + "/" + newKeyName;
					auto keyOld = pair.first;
					auto result = false;
					auto description = readFileData(groupName, keyOld, QString(), comment_file_, &result);
					auto value = pair.second;
					auto contains = false;
					for (int j = 0; j < properties.size(); ++j) {
						if (properties[j].first == keyNew) {
							contains = true;
							break;
						}
					}

					if (contains) {
						continue;
					}

					writeFileData(groupName, keyNew, value, ini_file_);
					if (result) {
						writeFileData(groupName, keyNew, description, comment_file_);
					}
					fastRemove(groupName, keyOld);
				}
				else {
					auto contains = false;
					for (int j = 0; j < properties.size(); ++j) {
						if (properties[j].first == newKeyName) {
							contains = true;
							break;
						}
					}

					if (contains) {
						continue;
					}

					writeFileData(groupName, newKeyName, pair.second, ini_file_);
					auto result = false;
					auto description = readFileData(groupName, pair.first, QString(), comment_file_, &result);
					if (result) {
						writeFileData(groupName, newKeyName, pair.second, comment_file_);
					}
					fastRemove(groupName, pair.first);
				}
			}
		}
	}
}

bool Ini::contains(const QString& key) const
{
	return contains(key, 0);
}

bool Ini::isGroup(const QString& key) const
{
	QString groupName, keyName;
	buildGroupAndKeyName(key, groupName, keyName);

	if (groupName.isEmpty()) {
		return childGroups().contains(key);
	}

	auto properties = childProperties(groupName);
	for (const auto& x : properties) {
		auto index = x.first.indexOf(keyName);
		if (index != -1 && x.first.indexOf(keyName + "/") != -1) {
			// 再次确认是否为数组
			auto findIndex = false, findSize = false;
			for (const auto& y : properties) {
				if (y.first.indexOf(keyName + "/1/") != -1) {
					findIndex = true;
				}

				if (y.first.indexOf(keyName + "/size") != -1) {
					findSize = true;
				}
			}

			return !(findIndex && findSize);
		}
		//else if (index != -1) {
		//	// 判断size
		//	return false;
		//}
	}

	return false;
}

bool Ini::isArray(const QString& key) const
{
	QString groupName, keyName;
	buildGroupAndKeyName(key, groupName, keyName);

	if (groupName.isEmpty()) {
		return false;
	}

	auto properties = childProperties(groupName);
	for (const auto& x : properties) {
		auto index = x.first.indexOf(keyName + "/");
		if (index != -1) {
			auto count = 1;
			auto size = 0;
			QList<int> nums;
			for (const auto& y : properties) {
				if (y.first.indexOf(keyName + QString("/%1/").arg(count)) != -1) {
					if (!nums.contains(count)) {
						nums.append(count++);
					}
				}

				if (y.first.indexOf(keyName + "/size") != -1) {
					size = y.second.toInt();
				}
			}

			if (nums.isEmpty() || size == 0) {
				return false;
			}

			// nums不可以<size,否则会导致数组越界崩溃.
			if (nums.size() < size) {
				return false;
			}

			count = 1;
			for (const auto& y : nums) {
				// 必须为连贯的
				if (count++ != y) {
					return false;
				}
			}
			return true;
		}
	}

	return false;
}

QStringList Ini::allKeys() const
{
	QMutexLocker locker(recursive_mutex_);

	QStringList result, keys;
	QString group, key;
	if (!ctx()->group.isEmpty() && ctx()->arrayPrefix.isEmpty()) {
		auto hindex = ctx()->group.indexOf("/");
		if (hindex != -1) {
			group = ctx()->group.mid(0, hindex);
			//auto tindex = ctx()->group.lastIndexOf("/");
			//if (tindex != -1 && tindex != hindex) {
			//	tail = ctx()->group.mid(tindex);
			//}
			//else {
			//	tail = ctx()->group.mid(hindex + 1);
			//}
			key = ctx()->group.mid(hindex + 1);
		}
		else {
			group = ctx()->group;
		}
		keys.append(group);
	}
	else if (ctx()->group.isEmpty() && !ctx()->arrayPrefix.isEmpty()) {
		auto index = ctx()->arrayPrefix.indexOf("/");
		if (index != -1) {
			group = ctx()->arrayPrefix.mid(0, index);
			key = ctx()->arrayPrefix.mid(index + 1);
			keys.append(group);
		}
		else {
			keys.append(ctx()->arrayPrefix);
		}
	}
	else if (!ctx()->group.isEmpty() && !ctx()->arrayPrefix.isEmpty()) {
		auto hindex = ctx()->group.indexOf("/");
		if (hindex != -1) {
			group = ctx()->group.mid(0, hindex);
			auto tindex = ctx()->group.lastIndexOf("/");
			//if (tindex != -1 && tindex != hindex) {
			//	key = ctx()->group.mid(tindex);
			//}
			//else {
			//	key = ctx()->group.mid(hindex + 1);
			//}
			key = ctx()->group.mid(hindex + 1);
			key += "/" + ctx()->arrayPrefix;
		}
		else {
			group = ctx()->group;
			key = ctx()->arrayPrefix;
		}
		keys.append(group);
	}
	else {
		keys = childGroups();
	}
	size_t size = 4096;
	std::unique_ptr<wchar_t[]> buffer = nullptr;

REALLOCATE_MEMORY:
	buffer.reset(new wchar_t[size]);
	memset(buffer.get(), 0x00, size * sizeof(wchar_t));

	for (const auto& x : keys) {
		auto app = x.toStdWString();
		fileLock();
		auto length = GetPrivateProfileSection(app.c_str(), buffer.get(), size, ini_file_.toStdWString().c_str());
		fileUnlock();
		if (length == size - 2) {
			size *= 2;
			buffer.reset();
			goto REALLOCATE_MEMORY;
		}

		auto split0 = QString::fromWCharArray(buffer.get(), length).split(QChar(0x00), QString::SkipEmptyParts);
		for (const auto& y : split0) {
			auto split1 = y.split("=", QString::SkipEmptyParts);
			if (ctx()->group.isEmpty()) {
				result.append(QString("%1/%2").arg(x, split1[0]).replace("\\", "/"));
			}
			else {
				result.append(split1[0].replace("\\", "/"));
			}
		}
	}

	if (!key.isEmpty()) {
		QStringList temp;
		for (int i = 0; i < result.size(); ++i) {
			auto index = result[i].indexOf(key + "/");
			if (index != -1) {
				temp.append(result[i].mid(index + key.length() + 1));
			}
		}
		result = temp;
	}

	if (key_sort_) {
		result.sort();
	}
	return result;
}

QStringList Ini::childKeys() const
{
	QMutexLocker locker(recursive_mutex_);

	QStringList result;
	if (!ctx()->group.isEmpty() && ctx()->arrayPrefix.isEmpty()) {
		size_t size = 4096;
		std::unique_ptr<wchar_t[]> buffer = nullptr;

	REALLOCATE_MEMORY0:
		buffer.reset(new wchar_t[size]);
		memset(buffer.get(), 0x00, size * sizeof(wchar_t));
		std::wstring app;
		auto firstSlash = ctx()->group.indexOf("/");
		QString childKey;
		if (firstSlash != -1) {
			app = ctx()->group.left(firstSlash).toStdWString();
			childKey = ctx()->group.mid(firstSlash + 1);
		}
		else {
			app = ctx()->group.toStdWString();
		}
		fileLock();
		auto length = GetPrivateProfileSection(app.c_str(), buffer.get(), size, ini_file_.toStdWString().c_str());
		fileUnlock();
		if (length == size - 2) {
			size *= 2;
			buffer.reset();
			goto REALLOCATE_MEMORY0;
		}

		auto split0 = QString::fromWCharArray(buffer.get(), length).split(QChar(0x00), QString::SkipEmptyParts);
		for (const auto& x : split0) {
			auto split1 = x.split("=", QString::SkipEmptyParts);
			result.append(split1[0].replace("\\", "/"));
		}

		QStringList temp;
		if (firstSlash != -1) {
			for (int i = 0; i < result.size(); ++i) {
				auto index = result[i].indexOf(childKey + "/");
				if (index != -1) {
					auto mid = result[i].mid(index + childKey.length() + 1);
					if (mid.indexOf("/") == -1) {
						temp.append(mid);
					}
				}
			}
		}
		else {
			for (int i = 0; i < result.size(); ++i) {
				auto index = result[i].indexOf("/");
				if (index == -1) {
					temp.append(result[i]);
				}
			}
		}
		result = temp;
	}
	else if (!ctx()->group.isEmpty() && !ctx()->arrayPrefix.isEmpty()) {
		size_t size = 4096;
		std::unique_ptr<wchar_t[]> buffer = nullptr;

	REALLOCATE_MEMORY1:
		buffer.reset(new wchar_t[size]);
		memset(buffer.get(), 0x00, size * sizeof(wchar_t));
		std::wstring app;
		auto firstSlash = ctx()->group.indexOf("/");
		QString childKey;
		if (firstSlash != -1) {
			app = ctx()->group.left(firstSlash).toStdWString();
			childKey = ctx()->group.mid(firstSlash + 1) + "/" + ctx()->arrayPrefix;
		}
		else {
			app = ctx()->group.toStdWString();
			childKey = ctx()->arrayPrefix;
		}

		if (ctx()->arrayIndex != -1) {
			childKey += "/" + QString::number(ctx()->arrayIndex + 1);
		}

		fileLock();
		auto length = GetPrivateProfileSection(app.c_str(), buffer.get(), size, ini_file_.toStdWString().c_str());
		fileUnlock();
		if (length == size - 2) {
			size *= 2;
			buffer.reset();
			goto REALLOCATE_MEMORY1;
		}

		auto split0 = QString::fromWCharArray(buffer.get(), length).split(QChar(0x00), QString::SkipEmptyParts);
		for (const auto& x : split0) {
			auto split1 = x.split("=", QString::SkipEmptyParts);
			result.append(split1[0].replace("\\", "/"));
		}

		QStringList temp;
		for (int i = 0; i < result.size(); ++i) {
			auto index = result[i].indexOf(childKey + "/");
			if (index != -1) {
				auto mid = result[i].mid(index + childKey.length() + 1);
				if (mid.indexOf("/") == -1) {
					temp.append(mid);
				}
			}
		}

		result = temp;
	}
	else if (ctx()->group.isEmpty() && !ctx()->arrayPrefix.isEmpty()) {
		size_t size = 4096;
		std::unique_ptr<wchar_t[]> buffer = nullptr;

	REALLOCATE_MEMORY2:
		buffer.reset(new wchar_t[size]);
		memset(buffer.get(), 0x00, size * sizeof(wchar_t));
		std::wstring app;
		QString childKey;
		auto firstSlash = ctx()->arrayPrefix.indexOf("/");
		if (firstSlash != -1) {
			app = ctx()->arrayPrefix.mid(0, firstSlash).toStdWString();
			childKey = ctx()->arrayPrefix.mid(firstSlash + 1);
			if (ctx()->arrayIndex != -1) {
				childKey += "/" + QString::number(ctx()->arrayIndex + 1);
			}
		}
		else {
			app = ctx()->arrayPrefix.toStdWString();
			if (ctx()->arrayPrefix != -1) {
				childKey = QString::number(ctx()->arrayIndex + 1);
			}
		}

		fileLock();
		auto length = GetPrivateProfileSection(app.c_str(), buffer.get(), size, ini_file_.toStdWString().c_str());
		fileUnlock();
		if (length == size - 2) {
			size *= 2;
			buffer.reset();
			goto REALLOCATE_MEMORY2;
		}

		auto split0 = QString::fromWCharArray(buffer.get(), length).split(QChar(0x00), QString::SkipEmptyParts);
		for (const auto& x : split0) {
			auto split1 = x.split("=", QString::SkipEmptyParts);
			result.append(split1[0].replace("\\", "/"));
		}

		QStringList temp;
		for (int i = 0; i < result.size(); ++i) {
			if (!childKey.isEmpty()) {
				auto str = childKey + "/";
				auto index = result[i].indexOf(str);
				if (index != -1 && result[i].left(str.length()) == str) {
					auto mid = result[i].mid(index + childKey.length() + 1);
					if (mid.indexOf("/") == -1) {
						temp.append(mid);
					}
				}
			}
			else {
				if (result[i].indexOf("/") == -1) {
					temp.append(result[i]);
				}
			}
		}

		result = temp;
	}

	if (key_sort_) {
		result.sort();
	}
	return result;
}

QStringList Ini::childGroups() const
{
	QMutexLocker locker(recursive_mutex_);

	QStringList result;
	if (ctx()->group.isEmpty() && ctx()->arrayPrefix.isEmpty()) {
		size_t size = 4096;
		std::unique_ptr<wchar_t[]> buffer;

	REALLOCATE_MEMORY0:
		buffer.reset(new wchar_t[size]);
		memset(buffer.get(), 0, size * sizeof(wchar_t));
		fileLock();
		auto length = GetPrivateProfileSectionNames(buffer.get(), size, ini_file_.toStdWString().c_str());
		fileUnlock();
		if (length == size - 2) {
			size *= 2;
			buffer.reset();
			goto REALLOCATE_MEMORY0;
		}

		result = QString::fromWCharArray(buffer.get(), length).split(QChar(0x00), QString::SkipEmptyParts);
	}
	else if (ctx()->group.isEmpty() && !ctx()->arrayPrefix.isEmpty()) {
		//error where
		QString group, key;
		auto firstSlash = ctx()->arrayPrefix.indexOf("/");
		if (firstSlash != -1) {
			group = ctx()->arrayPrefix.mid(0, firstSlash);
			key = ctx()->arrayPrefix.mid(firstSlash + 1);
		}
		else {
			group = ctx()->arrayPrefix;
		}

		auto properties = childProperties(group);
		for (const auto& x : properties) {
			// 特殊情况不需要跨越整个ctx()->arrayPrefix长度
			if (key.isEmpty()) {
				auto index = x.first.indexOf("/");
				if (index != -1) {
					auto arrayIndex = x.first.mid(0, index);
					if (!result.contains(arrayIndex)) {
						result.append(arrayIndex);
					}
				}
			}
			else {
				auto index = x.first.indexOf(key + "/");
				if (index != -1) {
					auto arrayIndex = x.first.mid(key.length() + 1);
					index = arrayIndex.indexOf("/");
					if (index != -1) {
						arrayIndex = arrayIndex.mid(0, index);
						if (!result.contains(arrayIndex)) {
							result.append(arrayIndex);
						}
					}
				}
			}
		}
	}
	else if (!ctx()->group.isEmpty() && !ctx()->arrayPrefix.isEmpty()) {
		QString group, key;
		auto firstSlash = ctx()->group.indexOf("/");
		if (firstSlash != -1) {
			key = ctx()->group.mid(firstSlash + 1) + "/" + ctx()->arrayPrefix;
			group = ctx()->group.mid(0, firstSlash);
		}
		else {
			group = ctx()->group;
			key = ctx()->arrayPrefix;
		}

		auto properties = childProperties(group);
		for (const auto& x : properties) {
			auto index = x.first.indexOf(key + "/");
			if (index != -1) {
				auto first = x.first.mid(key.length() + 1);
				index = first.indexOf("/");
				if (index != -1) {
					auto group = first.mid(0, index);
					if (!result.contains(group)) {
						result.append(group);
					}
				}
			}
		}
	}
	else {
		size_t size = 4096;
		std::unique_ptr<wchar_t[]> buffer = nullptr;

	REALLOCATE_MEMORY1:
		buffer.reset(new wchar_t[size]);
		memset(buffer.get(), 0x00, size * sizeof(wchar_t));
		std::wstring app;
		auto firstSlash = ctx()->group.indexOf("/");
		QString childKey;
		if (firstSlash != -1) {
			app = ctx()->group.left(firstSlash).toStdWString();
			childKey = ctx()->group.mid(firstSlash + 1);
		}
		else {
			app = ctx()->group.toStdWString();
		}
		fileLock();
		auto length = GetPrivateProfileSection(app.c_str(), buffer.get(), size, ini_file_.toStdWString().c_str());
		fileUnlock();
		if (length == size - 2) {
			size *= 2;
			buffer.reset();
			goto REALLOCATE_MEMORY1;
		}

		auto split0 = QString::fromWCharArray(buffer.get(), length).split(QChar(0x00), QString::SkipEmptyParts);
		for (const auto& x : split0) {
			auto split1 = x.split("=", QString::SkipEmptyParts);
			result.append(split1[0].replace("\\", "/"));
		}

		QStringList temp;
		if (firstSlash != -1) {
			for (int i = 0; i < result.size(); ++i) {
				auto index = result[i].indexOf(childKey + "/");
				if (index != -1) {
					auto mid = result[i].mid(index + childKey.length() + 1);
					if ((index = mid.indexOf("/")) != -1) {
						auto v = mid.mid(0, index);
						if (!temp.contains(v)) {
							temp.append(v);
						}
					}
				}
			}
		}
		else {
			for (int i = 0; i < result.size(); ++i) {
				auto index = result[i].indexOf("/");
				if (index != -1) {
					auto v = result[i].mid(0, index);
					if (!temp.contains(v)) {
						temp.append(v);
					}
				}
			}
		}
		result = temp;
	}

	if (key_sort_) {
		result.sort();
	}
	return result;
}

void Ini::enableKeySort(bool enable)
{
	key_sort_ = enable;
}

int Ini::ctxCount() const
{
	QMutexLocker locker(&ctx_mutex_);
	return ctx_map_.count();
}

bool Ini::contains(const QString& key, int flag) const
{
	QMutexLocker locker(recursive_mutex_);

	QString groupName;
	QString keyName;
	buildGroupAndKeyName(key, groupName, keyName);

	if (groupName.isEmpty()) {
		// 特殊情况需要单独处理
		groupName = key;
		keyName = "";
	}

	QString filePath;
	switch (flag)
	{
	case 0:
		filePath = ini_file_;
		break;
	case 1:
		filePath = comment_file_;
		break;
	default:
		break;
	}

	return containsFileData(groupName, keyName, filePath);
}

static const std::string base64_chars =
"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
"abcdefghijklmnopqrstuvwxyz"
"0123456789+/";

std::string Ini::base64Encode(const uint8_t* data, size_t length) const
{
	std::string encoded;
	int i = 0, j = 0;
	BYTE byte_array_3[3]{}, byte_array_4[4]{};

	while (length--) {
		byte_array_3[i++] = *(data++);
		if (i == 3) {
			byte_array_4[0] = (byte_array_3[0] & 0xfc) >> 2;
			byte_array_4[1] = ((byte_array_3[0] & 0x03) << 4) + ((byte_array_3[1] & 0xf0) >> 4);
			byte_array_4[2] = ((byte_array_3[1] & 0x0f) << 2) + ((byte_array_3[2] & 0xc0) >> 6);
			byte_array_4[3] = byte_array_3[2] & 0x3f;

			for (i = 0; i < 4; i++)
				encoded += base64_chars[byte_array_4[i]];
			i = 0;
		}
	}

	if (i) {
		for (j = i; j < 3; j++)
			byte_array_3[j] = '\0';

		byte_array_4[0] = (byte_array_3[0] & 0xfc) >> 2;
		byte_array_4[1] = ((byte_array_3[0] & 0x03) << 4) + ((byte_array_3[1] & 0xf0) >> 4);
		byte_array_4[2] = ((byte_array_3[1] & 0x0f) << 2) + ((byte_array_3[2] & 0xc0) >> 6);
		byte_array_4[3] = byte_array_3[2] & 0x3f;

		for (j = 0; j < i + 1; j++)
			encoded += base64_chars[byte_array_4[j]];

		while (i++ < 3)
			encoded += '=';
	}

	return encoded;
}

std::vector<uint8_t> Ini::base64Decode(const std::string& encoded) const
{
	std::vector<BYTE> decoded;
	int in_len = encoded.size();
	int i = 0, j = 0, in_ = 0;
	BYTE byte_array_4[4]{}, byte_array_3[3]{};

	while (in_len-- && (encoded[in_] != '=') && isBase64(encoded[in_])) {
		byte_array_4[i++] = encoded[in_];
		in_++;
		if (i == 4) {
			for (i = 0; i < 4; i++)
				byte_array_4[i] = static_cast<BYTE>(base64_chars.find(byte_array_4[i]));

			byte_array_3[0] = (byte_array_4[0] << 2) + ((byte_array_4[1] & 0x30) >> 4);
			byte_array_3[1] = ((byte_array_4[1] & 0xf) << 4) + ((byte_array_4[2] & 0x3c) >> 2);
			byte_array_3[2] = ((byte_array_4[2] & 0x3) << 6) + byte_array_4[3];

			for (i = 0; i < 3; i++)
				decoded.push_back(byte_array_3[i]);
			i = 0;
		}
	}

	if (i) {
		for (j = i; j < 4; j++)
			byte_array_4[j] = 0;

		for (j = 0; j < 4; j++)
			byte_array_4[j] = static_cast<BYTE>(base64_chars.find(byte_array_4[j]));

		byte_array_3[0] = (byte_array_4[0] << 2) + ((byte_array_4[1] & 0x30) >> 4);
		byte_array_3[1] = ((byte_array_4[1] & 0xf) << 4) + ((byte_array_4[2] & 0x3c) >> 2);
		byte_array_3[2] = ((byte_array_4[2] & 0x3) << 6) + byte_array_4[3];

		for (j = 0; j < i - 1; j++)
			decoded.push_back(byte_array_3[j]);
	}

	return decoded;
}

bool Ini::isBase64(uchar c) const
{
	return (isalnum(c) || (c == '+') || (c == '/'));
}


void Ini::createCrypt()
{
	uint8_t iniAesPwdBuf[] =
	{
		0xab,0xbc,0xcd,0xde,0xac,0xf0,0xff,0xbd,
		0x20,0x1d,0x7d,0x6b,0x9d,0x3d,0x4d,0xb0
	};

	if (encrypt_data_) {
		::CryptAcquireContext(&crypt_prov_, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT);
		::CryptCreateHash(crypt_prov_, CALG_MD5, NULL, 0, &crypt_hash_);
		::CryptHashData(crypt_hash_, iniAesPwdBuf, sizeof(iniAesPwdBuf), 0);
		::CryptDeriveKey(crypt_prov_, CALG_AES_128, crypt_hash_, CRYPT_EXPORTABLE, &crypt_key_);
	}
}

void Ini::destroyCrypt()
{
	// 析构函数不需要加锁，因为此时对象应该不再被共享
	if (encrypt_data_) {
		if (crypt_key_) {
			::CryptDestroyKey(crypt_key_);
		}

		if (crypt_hash_) {
			::CryptDestroyHash(crypt_hash_);
		}

		if (crypt_prov_) {
			::CryptReleaseContext(crypt_prov_, 0);
		}
	}
}

QString Ini::encryptData(const QString& data) const
{
	QString result;
	auto byte = data.toUtf8();
	DWORD size = byte.size();
	DWORD padding = 16 - (size % 16);
	if (padding == 0) {
		padding = 16;
	}
	DWORD bufSize = byte.size() + padding;

RENEW_MEMORY:
	BYTE* buf = new BYTE[bufSize];
	memcpy(buf, byte.constData(), byte.size());
	memset(buf + size, padding, padding);
	if (!::CryptEncrypt(crypt_key_, NULL, TRUE, 0, buf, &size, bufSize)) {
		auto code = GetLastError();
		if (code == NTE_BAD_LEN) {
			delete[] buf;
			bufSize *= 2;
			goto RENEW_MEMORY;
		}
		else if (code == NTE_BAD_DATA) {
			result = data;
		}
	}
	else {
		result = QString::fromStdString(base64Encode(buf, size));
	}
	delete[] buf;
	return result;
}

QString Ini::decryptData(const QString& data) const
{
	QString result;
	auto vec = base64Decode(data.toStdString());
	if (vec.size() != 0) {
		DWORD bufSize = vec.size();
	RENEW_MEMORY:
		BYTE* buf = new BYTE[bufSize];
		memcpy(buf, vec.data(), vec.size());
		if (!::CryptDecrypt(crypt_key_, NULL, TRUE, 0, buf, &bufSize)) {
			auto code = GetLastError();
			if (code == NTE_BAD_LEN) {
				delete[] buf;
				bufSize *= 2;
				goto RENEW_MEMORY;
			}
			else if (code == NTE_BAD_DATA) {
				result = data;
			}
		}
		else {
			result = QByteArray(reinterpret_cast<char*>(buf), bufSize);
		}
		delete[] buf;
	}
	else {
		result = data;
	}
	return result;
}

QVector<QPair<QString, QString>> Ini::childProperties(const QString& group) const
{
	QVector<QPair<QString, QString>> result;
	size_t size = 4096;
	std::unique_ptr<wchar_t[]> buffer = nullptr;

REALLOCATE_MEMORY0:
	buffer.reset(new wchar_t[size]);
	memset(buffer.get(), 0x00, size * sizeof(wchar_t));

	auto app = group.toStdWString();
	fileLock();
	auto length = GetPrivateProfileSection(app.c_str(), buffer.get(), size, ini_file_.toStdWString().c_str());
	fileUnlock();
	if (length == size - 2) {
		size *= 2;
		buffer.reset();
		goto REALLOCATE_MEMORY0;
	}

	auto split0 = QString::fromWCharArray(buffer.get(), length).split(QChar(0x00), QString::SkipEmptyParts);
	for (const auto& y : split0) {
		auto index = y.indexOf("=");
		if (index != -1) {
			auto key = y.mid(0, index).replace("\\", "/");
			auto value = y.mid(index + 1);
			if (encrypt_data_) {
				value = decryptData(value);
			}
			result.append(qMakePair(key, value));
		}
	}

	return result;
}

Ini::Ctx* Ini::ctx() const
{
	QMutexLocker locker(&ctx_mutex_);
	Qt::HANDLE threadId = QThread::currentThreadId();
	return &ctx_map_[threadId];
}

void Ini::clearCtx()
{
	auto tid = QThread::currentThreadId();
	QMutexLocker locker(&ctx_mutex_);
	if (ctx_map_.contains(tid)) {
		ctx_map_.remove(tid);
	}
}

void Ini::fileLock() const
{
	ini::file_lock[ini_file_].lock();
}

void Ini::fileUnlock() const
{
	ini::file_lock[ini_file_].unlock();
}

QString Ini::fastRead(const QString& group, const QString& key, const QString& defaultValue) const
{
	return readFileData(group, key, defaultValue, ini_file_);
}

void Ini::fastWrite(const QString& group, const QString& key, const QString& value) const
{
	writeFileData(group, key, value, ini_file_);
}

void Ini::fastRemove(const QString& group, const QString& key) const
{
	removeFileData(group, key, ini_file_);
	removeFileData(group, key, comment_file_);
}

void Ini::fastRename(const QString& group, const QString& oldKeyPath, const QString& newKeyName)
{
	// 存在bug
	auto func = [&](const QString& filePath) {
		auto result = false;
		auto value = readFileData(group, oldKeyPath, QString(), filePath, &result);
		if (!result) {
			return;
		}

		if (!removeFileData(group, oldKeyPath, filePath)) {
			return;
		}

		QString newKeyPath;
		auto lastSlash = oldKeyPath.lastIndexOf("/");
		if (lastSlash != -1) {
			newKeyPath = oldKeyPath.mid(0, lastSlash) + "/" + newKeyName;
		}
		else {
			newKeyPath = newKeyName;
		}

		writeFileData(group, newKeyPath, value, filePath);
		};

	func(ini_file_);
	func(comment_file_);
}

QString Ini::readFileData(const QString& group, const QString& key, const QString& defaultValue, const QString& filePath, bool* result) const
{
	QFileInfo fi(filePath);
	if (!fi.exists()) {
		if (result) {
			*result = false;
		}
		return defaultValue;
	}

	size_t size = 4096;
	std::unique_ptr<wchar_t[]> buffer;

REALLOCATE_MEMORY:
	buffer.reset(new wchar_t[size]);
	memset(buffer.get(), 0x00, size * sizeof(wchar_t));

	auto wkey = key.toStdWString();
	std::replace_if(wkey.begin(), wkey.end(), [](wchar_t c) { return c == L'/'; }, L'\\');

	fileLock();
	auto length = GetPrivateProfileString(group.toStdWString().c_str(), wkey.c_str(),
		defaultValue.toStdWString().c_str(), buffer.get(), size, filePath.toStdWString().c_str());
	fileUnlock();
	if (length == -1) {
		size *= 2;
		buffer.reset();
		goto REALLOCATE_MEMORY;
	}

	auto value = QString::fromWCharArray(buffer.get());
	if (encrypt_data_ && !value.isEmpty() && (filePath == ini_file_)) {
		value = decryptData(value);
	}

	if (result) {
		*result = true;
	}
	return value;
}

bool Ini::writeFileData(const QString& group, const QString& key, const QString& value, const QString& filePath) const
{
	std::wstring wstr;
	if (encrypt_data_ && !value.isEmpty() && (filePath == ini_file_)) {
		wstr = encryptData(value).toStdWString();
	}
	else {
		wstr = value.toStdWString();
	}

	auto wkey = key.toStdWString();
	std::replace_if(wkey.begin(), wkey.end(), [](wchar_t c) { return c == L'/'; }, L'\\');

	auto result = FALSE;
	fileLock();
	result = WritePrivateProfileStringW(group.toStdWString().c_str(), wkey.c_str(), wstr.c_str(), filePath.toStdWString().c_str());
	fileUnlock();
	return result;
}

bool Ini::removeFileData(const QString& group, const QString& key, const QString& filePath) const
{
	QFileInfo fi(filePath);
	if (!fi.exists()) {
		return false;
	}

	auto result = FALSE;
	if (!group.isEmpty() && !key.isEmpty()) {
		auto wkey = key.toStdWString();
		std::replace_if(wkey.begin(), wkey.end(), [](wchar_t c) { return c == L'/'; }, L'\\');
		fileLock();
		result = WritePrivateProfileStringW(group.toStdWString().c_str(), wkey.c_str(), nullptr, filePath.toStdWString().c_str());
		fileUnlock();
	}
	else if (!group.isEmpty() && key.isEmpty()) {
		fileLock();
		result = WritePrivateProfileStringW(group.toStdWString().c_str(), nullptr, nullptr, filePath.toStdWString().c_str());
		fileUnlock();
	}

	return result == TRUE;
}

bool Ini::containsFileData(const QString& group, const QString& key, const QString& filePath) const
{
	QFileInfo fi(filePath);
	if (!fi.exists()) {
		return false;
	}

	bool result = false;
	if (key.isEmpty()) {
		size_t size = 4096;
		std::unique_ptr<wchar_t[]> buffer;
	REALLOCATE_MEMORY0:
		buffer.reset(new wchar_t[size]);
		memset(buffer.get(), 0, size * sizeof(wchar_t));
		fileLock();
		auto length = GetPrivateProfileSectionNames(buffer.get(), size, ini_file_.toStdWString().c_str());
		fileUnlock();
		if (length == size - 2) {
			size *= 2;
			buffer.reset();
			goto REALLOCATE_MEMORY0;
		}

		auto split = QString::fromWCharArray(buffer.get(), length).split(QChar(0x00), QString::SkipEmptyParts);
		result = split.contains(group, Qt::CaseInsensitive);
	}
	else {
		size_t size = 4096;
		std::unique_ptr<wchar_t[]> buffer = nullptr;

	REALLOCATE_MEMORY1:
		buffer.reset(new wchar_t[size]);
		memset(buffer.get(), 0x00, size * sizeof(wchar_t));

		fileLock();
		auto length = GetPrivateProfileSection(group.toStdWString().c_str(), buffer.get(), size, filePath.toStdWString().c_str());
		fileUnlock();
		if (length == size - 2) {
			size *= 2;
			buffer.reset();
			goto REALLOCATE_MEMORY1;
		}

		bool find = false;
		auto split0 = QString::fromWCharArray(buffer.get(), length).split(QChar(0x00), QString::SkipEmptyParts);
		for (const auto& x : split0) {
			auto split = x.split("=", QString::SkipEmptyParts);
			if (split.size() != 0 && split[0].replace("\\", "/").compare(key, Qt::CaseInsensitive) == 0) {
				find = true;
				break;
			}
		}
		result = find;
	}

	return result;
}
