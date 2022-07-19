#include <cassert>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rttypes {
namespace {
    constexpr size_t padding(size_t offset, size_t alignment)
    {
        const auto misalignment = offset & alignment;
        return misalignment > 0 ? alignment - misalignment : 0;
    }

    constexpr size_t align(size_t offset, size_t alignment)
    {
        return offset + padding(offset, alignment);
    }

    template <typename T>
    auto offset(T* ptr, size_t offset)
    {
        return static_cast<T*>(static_cast<uint8_t*>(ptr) + offset);
    }

    template <typename T>
    auto offset(const T* ptr, size_t offset)
    {
        return static_cast<const T*>(static_cast<const uint8_t*>(ptr) + offset);
    }
}

class Type {
public:
    Type() = default;

    Type(size_t size, size_t alignment)
        : size_(size)
        , alignment_(alignment)
    {
    }

    // This is needed so we can copy structs easily
    virtual std::unique_ptr<Type> copy() const = 0;

    virtual void copyData(void* dest, const void* src) const = 0;

    virtual void construct(void* ptr) const = 0;
    virtual void destruct(void* ptr) const = 0;

    size_t size() const { return size_; } // including padding, like sizeof
    size_t alignment() const { return alignment_; }

protected:
    size_t size_ = 0;
    size_t alignment_ = 0;
};

template <typename T>
class ConcreteType : public Type {
public:
    using Underlying = T;

    ConcreteType()
        : Type(sizeof(T), std::alignment_of_v<T>)
    {
    }

    ConcreteType(const ConcreteType&) = default;

    T& view(void* ptr) const { return *reinterpret_cast<T*>(ptr); }

    std::unique_ptr<Type> copy() const override { return std::make_unique<ConcreteType>(*this); }

    void copyData(void* dest, const void* src) const override
    {
        construct(dest);
        view(dest) = *reinterpret_cast<const T*>(src);
    }

    void construct(void* ptr) const override { new (ptr) T {}; }
    void destruct(void* ptr) const override { reinterpret_cast<T*>(ptr)->~T(); }
};

using Float32 = ConcreteType<float>;
using String = ConcreteType<std::string>;

class Struct : public Type {
public:
    Struct() = default;
    ~Struct() = default;

    Struct(const Struct& other)
        : Type(other.size_, other.alignment_)
        , currentOffset_(other.currentOffset_)
    {
        for (const auto& field : other.fields_) {
            fields_.push_back(Field { field.name, field.type->copy(), field.offset });
        }
    }

    struct Field {
        std::string name;
        std::unique_ptr<Type> type;
        size_t offset;
    };

    struct View {
    public:
        View(const Struct* st, void* ptr)
            : struct_(st)
            , ptr_(ptr)
        {
        }

        void* fieldPtr(size_t index) { return offset(ptr_, struct_->fields_[index].offset); }

        void* fieldPtr(std::string_view name)
        {
            return fieldPtr(struct_->getFieldIndex(name).value());
        }

        template <typename T>
        T& field(size_t index)
        {
            return *reinterpret_cast<T*>(fieldPtr(index));
        }

        template <typename T>
        T& field(std::string_view name)
        {
            return field<T>(struct_->getFieldIndex(name).value());
        }

    private:
        const Struct* struct_;
        void* ptr_;
    };

    template <typename FieldType>
    size_t addField(std::string name, const FieldType& type)
    {
        currentOffset_ = align(currentOffset_, type.alignment());
        fields_.push_back(Field { std::move(name), type.copy(), currentOffset_ });
        currentOffset_ += type.size();

        alignment_ = std::max(alignment_, type.alignment());
        size_ = align(currentOffset_, alignment_);

        return fields_.size() - 1;
    }

    std::optional<size_t> getFieldIndex(std::string_view name) const
    {
        for (size_t i = 0; i < fields_.size(); ++i) {
            if (fields_[i].name == name) {
                return i;
            }
        }
        return std::nullopt;
    }

    View view(void* ptr) const { return View(this, ptr); }
    // ConstView view(const void* ptr) const { return View(this, ptr); }

    const Field& field(size_t index) const { return fields_[index]; }
    const Field& field(std::string_view name) const { return fields_[getFieldIndex(name).value()]; }

    std::unique_ptr<Type> copy() const override { return std::make_unique<Struct>(*this); }

    void copyData(void* dest, const void* src) const override
    {
        construct(dest);
        for (const auto& field : fields_) {
            field.type->copyData(offset(dest, field.offset), offset(src, field.offset));
        }
    }

    void construct(void* ptr) const override
    {
        for (const auto& field : fields_) {
            field.type->construct(offset(ptr, field.offset));
        }
    }

    void destruct(void* ptr) const override
    {
        for (const auto& field : fields_) {
            field.type->destruct(offset(ptr, field.offset));
        }
    }

private:
    std::vector<Field> fields_;
    size_t currentOffset_ = 0;
};

class VectorData {
public:
    template <typename ElementType>
    VectorData(const ElementType& elementType)
        : elementType_(elementType.copy())
    {
    }

    ~VectorData()
    {
        resize(0);
        delete[] data_;
    }

    VectorData& operator=(const VectorData& other)
    {
        resize(other.size_);
        for (size_t i = 0; i < other.size_; ++i) {
            elementType_->copyData(indexPtr(i), other.indexPtr(i));
        }
        return *this;
    }

    void* indexPtr(size_t idx) { return data_ + idx * elementType_->size(); }
    const void* indexPtr(size_t idx) const { return data_ + idx * elementType_->size(); }

    template <typename T>
    T& index(size_t idx)
    {
        assert(sizeof(T) == elementType_->size());
        assert(idx < size_);
        return *reinterpret_cast<T*>(indexPtr(idx));
    }

    void grow(size_t num = 1) { resize(size_ + num); }

    void resize(size_t newSize)
    {
        if (newSize > size_) {
            if (capacity_ < newSize) {
                const auto newCapacity = std::max(size_ * 2, newSize);
                const auto newData = new uint8_t[newCapacity * elementType_->size()];
                for (size_t i = 0; i < size_; ++i) {
                    elementType_->copyData(newData + i * elementType_->size(), indexPtr(i));
                }
                delete[] data_;
                data_ = newData;
                capacity_ = newCapacity;
            }
            std::memset(indexPtr(size_), 0, (newSize - size_) * elementType_->size());
            for (size_t i = size_; i < newSize; ++i) {
                elementType_->construct(indexPtr(i));
            }
        } else {
            for (size_t i = newSize; i < size_; ++i) {
                elementType_->destruct(indexPtr(i));
            }
        }
        size_ = newSize;
    }

    template <typename T = void>
    T* data()
    {
        return data_;
    }

    size_t size() const { return size_; }

    size_t capacity() const { return capacity_; }

    Type* elementType() const { return elementType_.get(); }

private:
    std::unique_ptr<Type> elementType_;
    uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t capacity_ = 0; // size of data_ is capacity_ * elementType_->size()
};

class Vector : public Type {
public:
    template <typename ElementType>
    Vector(const ElementType& elementType)
        : Type(sizeof(VectorData), std::alignment_of_v<VectorData>)
        , elementType_(elementType.copy())
    {
    }

    Vector(const Vector& other)
        : Type(sizeof(VectorData), std::alignment_of_v<VectorData>)
        , elementType_(other.elementType_->copy())
    {
    }

    VectorData& view(void* ptr) const { return *reinterpret_cast<VectorData*>(ptr); }

    std::unique_ptr<Type> copy() const override { return std::make_unique<Vector>(*this); }

    void copyData(void* dest, const void* src) const override
    {
        construct(dest);
        view(dest) = *reinterpret_cast<const VectorData*>(src);
    }

    void construct(void* ptr) const override { new (ptr) VectorData { *elementType_ }; }
    void destruct(void* ptr) const override { reinterpret_cast<VectorData*>(ptr)->~VectorData(); }

private:
    std::unique_ptr<Type> elementType_;
};
}

#include <array>
#include <iostream>

std::string hex(const uint8_t* data, size_t len)
{
    static constexpr std::array<char, 16> hexChars { '0', '1', '2', '3', '4', '5', '6', '7', '8',
        '9', 'a', 'b', 'c', 'd', 'e', 'f' };
    std::string ret;
    for (size_t i = 0; i < len; ++i) {
        ret.push_back(hexChars[(data[i] & 0xF0) >> 4]);
        ret.push_back(hexChars[(data[i] & 0x0F) >> 0]);
    }
    return ret;
}

int main()
{
    rttypes::Struct vec;
    auto f1 = vec.addField("x", rttypes::Float32 {});
    vec.addField("y", rttypes::Float32 {});

    std::vector<uint8_t> vecBuf(vec.size());
    vec.construct(vecBuf.data());
    auto vecView = vec.view(vecBuf.data());
    auto& x = vecView.field<float>(f1);
    auto& y = vecView.field<float>("y");
    x = 69.0f;
    y = 42.0f;
    vec.destruct(vecBuf.data());

    for (auto ptr = vecBuf.data(); ptr < vecBuf.data() + vecBuf.size(); ptr += sizeof(float)) {
        std::cout << *reinterpret_cast<const float*>(ptr) << "\n";
    }

    rttypes::Struct line;
    line.addField("start", vec);
    line.addField("end", vec);
    line.addField("color", rttypes::String {});

    std::vector<uint8_t> lineBuf(line.size());
    line.construct(lineBuf.data());
    auto lineView = line.view(lineBuf.data());
    auto startView = vec.view(lineView.fieldPtr("start"));
    startView.field<float>("x") = 12.0f;
    startView.field<float>("y") = 13.0f;
    auto endView = vec.view(lineView.fieldPtr("end"));
    endView.field<float>("x") = 20.0f;
    endView.field<float>("y") = 21.0f;
    lineView.field<rttypes::String::Underlying>("color") = "green";
    line.destruct(lineBuf.data());

    for (size_t i = 0; i < 4; ++i) {
        std::cout << reinterpret_cast<const float*>(lineBuf.data())[i] << "\n";
    }
    // You can see the string because of small buffer optimization
    const auto strStart = lineBuf.data() + sizeof(float) * 4;
    const auto strLen = sizeof(std::string);
    std::cout << hex(strStart, strLen) << "\n";
    auto visible = [](char ch) { return ch >= ' ' && ch <= '~'; };
    for (size_t i = 0; i < strLen; ++i) {
        std::cout << " " << (visible(strStart[i]) ? static_cast<char>(strStart[i]) : ' ');
    }
    std::cout << "\n";

    rttypes::Vector numList(rttypes::Float32 {});
    std::vector<uint8_t> listBuf(numList.size());
    numList.construct(listBuf.data());
    auto& listView = numList.view(listBuf.data());
    listView.resize(4);
    listView.index<float>(0) = 1.0f;
    listView.index<float>(1) = 2.0f;
    listView.index<float>(2) = 3.0f;
    listView.index<float>(3) = 4.0f;
    numList.destruct(listBuf.data());
}
