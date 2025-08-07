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
* @brief ��չ��QVariant�֧࣬��JSON����ת��
* ����̳���QVariant�������˶�QStringList��QJsonObject��QJsonArray��QByteArray��֧�֣�
*/
class Variant : protected QVariant {
public:
	friend class Ini;

	enum class UserType {
		Range = QMetaType::User + 1,
	};

	// ���캯��
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
	// ����ת��Ϊx~y��ʽ,ʹ��toRangeת��
	template<typename T, std::enable_if_t<std::is_arithmetic_v<T> ||
		std::is_same_v<T, QString>, int> = 0>
	inline Variant(const QPair<T, T>&p) : QVariant() {
		range_pair_ = p;
		d = QVariant::Private(static_cast<int>(UserType::Range));
	}

	// ��������ת��
	using QVariant::toInt;
	using QVariant::toUInt;
	using QVariant::toLongLong;
	using QVariant::toULongLong;
	using QVariant::toBool;
	using QVariant::toDouble;
	using QVariant::toFloat;
	using QVariant::toString;

	// ��չ����ת��
	QStringList toStringList() const;
	QJsonObject toJsonObject() const;
	QJsonArray toJsonArray() const;
	QByteArray toByteArray() const;

	// ֧�ַ�Χ
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

	// ������Ϣ��ѯ
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
	// RAII����

	// ������
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

	// ��������
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

	// д������
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
	 * @brief ���캯��
	 * @param[in] filePath INI�ļ�·����Ϊ���򴴽��ڴ�INI
	 * @param[in] encryptData �Ƿ��������ݼ���
	 */
	explicit Ini(const QString& filePath = QString(), bool encryptData = false);

	/**
	 * @brief ��������
	 */
	~Ini();

	/*
	* @brief �������캯��
	*/
	Ini(const Ini& other);

	/*
	* @brief ��ֵ���캯��
	*/
	Ini& operator=(const Ini& other);

	// ɾ���ƶ�����
	Ini(Ini&&) = delete;

	// ɾ����ֵ�ƶ�����
	Ini& operator=(const Ini&&) = delete;

	/**
	 * @brief ��ȡ��ǰINI�ļ�·��
	 * @return INI�ļ�·��
	 */
	QString filePath() const;

	//=====================================================================
	// �����
	//=====================================================================

	/**
	 * @brief ��ʼһ���µķ���
	 * @param[in] prefix ����ǰ׺
	 *
	 * �����ļ�ֵ�������ڴ˷����½��У�����Ƕ�׵����Դ����༶���顣
	 */
	void beginGroup(const QString& prefix);

	/**
	 * @brief ������ǰ����
	 *
	 * ���ص���һ�����������ġ�
	 */
	void endGroup();

	/*
	* @brief �������з���
	*/
	void endAllGroup();

	/**
	 * @brief ��ȡ��ǰ����·��
	 * @return ��ǰ����·��
	 */
	QString group() const;

	//=====================================================================
	// �������
	//=====================================================================

	/**
	 * @brief ��ʼ��ȡ����
	 * @param[in] prefix ����ǰ׺
	 * @return �����С
	 *
	 * ���ڶ�ȡ�������飬��Ҫ���setArrayIndex��endArrayʹ�á�
	 */
	int beginReadArray(const QString& prefix);

	/**
	 * @brief ��ʼд������
	 * @param[in] prefix ����ǰ׺
	 * @param[in] size �����С��-1��ʾ�Զ�����
	 *
	 * ����д�������飬��Ҫ���setArrayIndex��endArrayʹ�á�
	 */
	void beginWriteArray(const QString& prefix, int size = -1);

	/**
	 * @brief �����������
	 *
	 * ��������д���������������С��Ϣ��
	 */
	void endArray();

	/**
	 * @brief ���õ�ǰ��������
	 * @param[in] i ��������
	 *
	 * ��beginReadArray��beginWriteArray֮����ã����õ�ǰ����������Ԫ��������
	 */
	void setArrayIndex(int i);

	/*
	* @brief ��������
	* @param[in] prefix ����ǰ׺
	* @param[in] func �ص�����(index, key, value)����falseֹͣ����
	* @note ��Ҫע��,�˽ӿڽ������beginReadArray��endArray,����Ѿ�������begin������ʧ��
	*/
	void traverseArray(const QString& prefix, IniTraverseArrayCb&& func);

	//=====================================================================
	// ��ֵ����
	//=====================================================================

	/**
	 * @brief ���ü�ֵ
	 * @param[in] key ����
	 * @param[in] value ֵ
	 */
	void setValue(const QString& key, const Variant& value);

	/**
	 * @brief ��ȡ��ֵ
	 * @param[in] key ����
	 * @param[in] defaultValue Ĭ��ֵ
	 * @return ����Ӧ��ֵ�����������򷵻�Ĭ��ֵ
	 */
	Variant value(const QString& key, const Variant& defaultValue = Variant()) const;

	//=====================================================================
	// ע�Ͳ���
	//=====================================================================

	/**
	 * @brief ���ü���ע��
	 * @param[in] key ����
	 * @param[in] comment ע������
	 */
	void setComment(const QString& key, const QString& comment);

	/**
	 * @brief ��ȡ����ע��
	 * @param[in] key ����
	 * @param[in] defaultComment Ĭ��ע��
	 * @return ����Ӧ��ע�ͣ����������򷵻�Ĭ��ע��
	 */
	QString comment(const QString& key, const QString& defaultComment = QString()) const;

	//=====================================================================
	// ��������
	//=====================================================================

	/**
	 * @brief ���ü�ֵ�����ע��
	 * @param[in] key ����
	 * @param[in] value ֵ
	 * @param[in] comment ע������
	 *
	 * �൱����������setValue��setComment��
	 */
	void newValue(const QString& key, const Variant& value, const QString& comment = QString());

	//=====================================================================
	// ������
	//=====================================================================

	/**
	 * @brief �Ƴ���
	 * @param[in] key ����
	 */
	void remove(const QString& key);

	/**
	 * @brief ��������
	 * @param[in] oldKeyPath �ɼ�·��
	 * @param[in] newKeyName �¼���
	 *
	 * ʾ��:
	 * rename("config/key0", "key"); //�ڶ�����������������, ��key0��Ϊkey, ��Ҫд��config/key
	 */
	void rename(const QString& oldKeyPath, const QString& newKeyName);

	/**
	 * @brief �����Ƿ����
	 * @param[in] key ����
	 * @return ���ڷ���true�����򷵻�false
	 */
	bool contains(const QString& key) const;

	/*
	* @brief �Ƿ�Ϊ����
	* @param[in] key ����
	* @return �Ƿ���true, ���򷵻�false
	*/
	bool isGroup(const QString& key) const;

	/*
	* @brief �Ƿ�Ϊ����
	* @param[in] key ����
	* @return �Ƿ񷵻�true, ���򷵻�false
	*/
	bool isArray(const QString& key) const;

	//=====================================================================
	// ��ѯ����
	//=====================================================================

	/**
	 * @brief ��ȡ���м�
	 * @return �������м����ַ����б�
	 */
	QStringList allKeys() const;

	/**
	 * @brief ��ȡ��ǰ������Ӽ�
	 * @return ������ǰ���������Ӽ����ַ����б�
	 */
	QStringList childKeys() const;

	/**
	 * @brief ��ȡ��ǰ���������
	 * @return ������ǰ��������������ַ����б�
	 */
	QStringList childGroups() const;

	/*
	* @brief ���ü�����
	* @param enable �Ƿ�����
	*/
	void enableKeySort(bool enable = true);

	/*
	* @brief �����ĵ�����
	* @return �����ĵ�����
	*/
	int ctxCount() const;

protected:
	/**
	 * @brief ���������������ͼ���
	 * @param[in] key �������
	 * @param[in] groupName �������
	 * @param[in] keyName �������
	 */
	void buildGroupAndKeyName(const QString& key, QString& groupName, QString& keyName) const;

	/**
	 * @brief �����Ƿ���ڵ��ڲ�����
	 * @param[in] key ����
	 * @param[in] flag ����־
	 * @return ���ڷ���true�����򷵻�false
	 */
	bool contains(const QString& key, int flag) const;

	//=====================================================================
	// �������
	//=====================================================================

	/**
	 * @brief Base64����
	 * @param[in] data ����������
	 * @param[in] length ���ݳ���
	 * @return �������ַ���
	 */
	std::string base64Encode(const uint8_t* data, size_t length) const;

	/**
	 * @brief Base64����
	 * @param[in] encoded �������ַ���
	 * @return ����������
	 */
	std::vector<uint8_t> base64Decode(const std::string& encoded) const;

	/**
	 * @brief �ж��ַ��Ƿ�ΪBase64�ַ�
	 * @param[in] c ���ж��ַ�
	 * @return �Ƿ���true�����򷵻�false
	 */
	bool isBase64(uchar c) const;

	/*
	* @brief ��������
	*/
	void createCrypt();

	/*
	* @brief ���ټ���
	*/
	void destroyCrypt();

	/*
	* @brief ��������
	* @param[in] data ��Ҫ���ܵ�����
	* @return ���ܺ������
	*/
	QString encryptData(const QString& data) const;

	/*
	* @brief ��������
	* @param[in] data ��Ҫ���ܵ�����
	* @return ���ܺ������
	*/
	QString decryptData(const QString& data) const;

	/*
	* @brief ��ȡ����������
	* @param[in] group ����
	* @return ����������
	*/
	QVector<QPair<QString, QString>> childProperties(const QString& group) const;

	// ������
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
	* @brief ��ȡ������
	* @return ������
	*/
	Ctx* ctx() const;

	/*
	* @brief ���������
	* @note ��������߳��н��в���ini�ļ���,�ȴ��߳��˳���ʱ��,��Ҫ���ô˺���������������
	* @note ֻ���������̵߳�������,���̵߳Ĳ��ᱻ����
	*/
	void clearCtx();

	/*
	* @brief �ļ�����
	*/
	void fileLock() const;

	/*
	* @brief �ļ�����
	*/
	void fileUnlock() const;

	/*
	* @brief ���ٶ�ȡ
	* @param[in] group ����
	* @param[in] key ��(�����ļ���)�ڲ���\���ֲ㼶, ��QSettingsһ��
	* @param[in] defaultValue Ĭ��ֵ
	* @return ��ȡ��ֵ
	*/
	QString fastRead(const QString& group, const QString& key, const QString& defaultValue = QString()) const;

	/*
	* @brief ����д��
	* @param[in] group ����
	* @param[in] key ��(�����ļ���)�ڲ���\���ֲ㼶, ��QSettingsһ��
	* @param[in] value д���ֵ
	*/
	void fastWrite(const QString& group, const QString& key, const QString& value) const;

	/*
	* @brief ����ɾ��
	* @param[in] group ����
	* @param[in] key ��(�����ļ���), ���Ϊ����ɾ��group, �ڲ���\���ֲ㼶, ��QSettingsһ��
	*/
	void fastRemove(const QString& group, const QString& key) const;

	/*
	* @brief ����������
	* @param[in] group ����
	* @param[in] oldKeyPath �ɼ�·��(�����ļ���)�ڲ���\���ֲ㼶, ��QSettingsһ��
	* @param[in] newKeyName �¼���
	*/
	void fastRename(const QString& group, const QString& oldKeyPath, const QString& newKeyName);

private:
	/*
	* @brief ��ȡ�ļ�����
	* @param[in] group ����
	* @param[in] key ��
	* @param[in] defaultValue Ĭ��ֵ
	* @param[in] filePath �ļ�·��
	* @param[out] result ��ȡ�Ľ��
	* @return ���ض�ȡ����ֵ
	*/
	QString readFileData(const QString& group, const QString& key, const QString& defaultValue, const QString& filePath, bool* result = nullptr) const;

	/*
	* @brief д���ļ�����
	* @param[in] group ����
	* @param[in] key ��
	* @param[in] value ֵ
	* @param[in] filePath �ļ�·��
	* @return �ɹ�����true, ʧ�ܷ���false
	*/
	bool writeFileData(const QString& group, const QString& key, const QString& value, const QString& filePath) const;

	/*
	* @brief �Ƴ��ļ�����
	* @param[in] group ����
	* @param[in] key ��
	* @param[in] filePath �ļ�·��
	* @return �ɹ�����true, ʧ�ܷ���false
	*/
	bool removeFileData(const QString& group, const QString& key, const QString& filePath) const;

	/*
	* @brief �����ļ�����
	* @param[in] group ����
	* @param[in] key ��
	* @param[in] filePath �ļ�·��
	* @return �ɹ�����true, ʧ�ܷ���false
	*/
	bool containsFileData(const QString& group, const QString& key, const QString& filePath) const;

private:
	//=====================================================================
	// ��Ա����
	//=====================================================================
	mutable QMap<Qt::HANDLE, Ctx> ctx_map_;        // �߳�������ӳ��
	mutable QMutex ctx_mutex_;                     // �߳������Ļ�����
	mutable QRecursiveMutex* recursive_mutex_;     // �ݹ黥��������֤�̰߳�ȫ
	QString ini_file_;                             // INI�ļ�·��
	QString comment_file_;                         // INIע���ļ�·��
	bool encrypt_data_;                            // �Ƿ�������ݱ�־
	bool key_sort_;                                // �Ƿ������
	ULONG_PTR crypt_prov_ = 0;                     // ���ܷ����ṩ�߾��
	ULONG_PTR crypt_hash_ = 0;                     // ��ϣ������
	ULONG_PTR crypt_key_ = 0;                      // ������Կ���
};

