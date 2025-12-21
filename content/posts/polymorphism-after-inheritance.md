+++
date = '2025-12-18T15:54:29+05:30'
title = 'Polymorphism After Inheritance Stops Working'
author = 'vibhatsu'
tags = ["c++", "design-patterns"]
cover = ""
coverCaption = "Change is the enemy of the perfect design."
description = "Using type erasure to achieve polymorphism in C++."
readingTime = true
comments = true
keywords = ["c++", "design patterns", "type erasure", "external polymorphism", "bridge pattern", "prototype pattern", "strategy pattern"]
+++
> *Change is the enemy of the perfect design.*

Recently, I had been involved in an implementation-heavy project where we had to design the architecture for a few components. It is my first project of this scale and we had quite the hard deadlines. We ended up using not so elegant solutions to get things done quickly. This did not satisfy me and I knew there should be better ways of doing things. Hence, I started researching for solutions and came across the concept of Type Erasure in C++. `Klaus Iglberger` has a great talk on this topic. I would like to share my understanding of Type Erasure through this blog post.

## The Problem
I will be using the very classic problem for these kinds of issues - the `Shape` problem (How original of me!). We have different shapes like `Circle`, `Square`, `Triangle` etc. Each shape has its own `draw()` method. We want to create a system where we can store different shapes in a single container and call their `draw()` methods without knowing their concrete types at compile time. Also, we want the system to have opportunities for future extensions. This is a classic case of polymorphism. The traditional way to achieve this in C++ is through inheritance and virtual functions. However, this approach has its limitations.
1. It requires all shapes to inherit from a common base class, which can lead to a rigid class hierarchy.
2. Suppose currently we are only relying on some graphic library like `OpenGL` for "drawing" the shapes but in future we might want to add support for another library like `vulkan`. In that case, our design fails spectacularly!

### Naive Approach: Inheritance and Virtual Functions
To solve this issue, let's create explicit derived classes for each shape and library we draw with. 
For example:
![sol1](../images/type-erasure/sol1.png)
As you can see, there are basically 3 levels of hierarchy here. The base class `Shape` has derived classes for each shape type like `Circle`, `Square` etc. Each of these derived classes has further derived classes for each graphics library like `OpenGLCircle`, `VulkanCircle` etc.

Now, this might actually work but as time passes, software changes and so does requirements. Now, suppose we need another functionality on shapes like `serialize()`. How can we add this functionality to our existing design?

Well, the only way is to extend the hierarchy further down and adding more derived classes as for `Circle` in the figure below:
![sol1-failure](../images/type-erasure/sol1-failure.png)

Issues:
1. As we add on more functionalities, the class hierarchy becomes deeper and more complex.
2. As we add more functionalities, the names of the classes become more and more ridiculous and complex.

What if we just make the derived classes like `OpenGLLittleEndianCircle`, etc. directly from special shape like in the `Square` example above? This would reduce the depth of the hierarchy but would lead to a combinatorial explosion of classes as we add more shapes and libraries and still the naming would very complex! 

To conclude, with this approach, we are still stuck with the following issues:
1. A lot of derived classes!
2. Ridiculous class names!
3. Deep class hierarchies!
4. Duplication between similar implementations!
5. (almost) Impossibility of adding new functionalities without modifying existing code!
6. Impeded code readability and maintainability!

At this point, the problem is no longer "too many classes". It is that adding a new operation requires modifying all existing abstractions. That is the moment where inheritance stops being a tool and starts being friction.

## Classic Solution: Design Patterns
> **Inheritance is rarely the answer.  
> Has-a trumps Is-a.**
>
> — *Andrew Hunt & David Thomas*,  
> *The Pragmatic Programmer*

Now, some of the readers may not be familiar with the term "design patterns". Design patterns are typical solutions to common problems in software design. 

A design pattern:
- has a **name**
- carries an **intent**
- aims at reducing **dependencies**
- provides some sort of **abstraction**
- has **proven to work** over the years

One of the best books to learn these is *Design Patterns: Elements of Reusable Object-Oriented Software* by the "Gang of Four". The book describes design patterns using Object-Oriented Programming but the concepts can be applied to other programming paradigms as well.

Back to our problem, for solving our issue, the design pattern that fits best is the `Strategy Pattern`. This pattern suggests selecting an algorithm polymorphically instead of hardcoding it. What we are trying to do is provide an interface for a method and ask the user to provide the strategy to use. It's basically equivalent to saying "I will do the work but you need to tell me how to do it".
Let's see how we can apply this pattern to our problem:
![classic-strat](../images/type-erasure/classic-strat.png)
Here, for the `Circle` class, we have an interface `DrawCircleStrategy` and there are concrete implementations of this interface for each graphics library like `OpenGLDrawCircleStrategy`, `VulkanDrawCircleStrategy` etc. Same goes for other shapes as well. The rough implementation would look something like this:
```cpp
class DrawCircleStrategy;
class Circle: public Shape {
private:
    std::unique_ptr<DrawCircleStrategy> drawStrategy;
    double radius;
public:
    Circle(double r, std::unique_ptr<DrawCircleStrategy> ds) : radius(r), drawStrategy(std::move(ds)) {}
    void draw() {
        drawStrategy->draw(*this);
    }
    double getRadius() const { return radius;}
};
class DrawCircleStrategy {
public:
    virtual void draw(Circle const&) = 0;
    virtual ~DrawStrategy() = default;
};
class OpenGLCircleStrategy : public DrawCircleStrategy {
public:
    void draw(Circle const& c) override {
        // OpenGL specific drawing code for Circle
    }
};
class VulkanCircleStrategy : public DrawCircleStrategy {
public:
    void draw(Circle const& c) override {
        // Vulkan specific drawing code for Circle
    }
};
// Do the same for Square
class DrawSquareStrategy;
class Square: public Shape {
private:
    std::unique_ptr<DrawSquareStrategy> drawStrategy;
    double side;
public:
    Square(double s, std::unique_ptr<DrawSquareStrategy> ds) : side(s), drawStrategy(std::move(ds)) {}
    void draw() {
        drawStrategy->draw(*this);
    }
    double getSide() const { return side;}
};
class DrawSquareStrategy {
public:
    virtual void draw(Square const&) = 0;
    virtual ~DrawSquareStrategy() = default;
};
class OpenGLSquareStrategy : public DrawSquareStrategy {
public:
    void draw(Square const& s) override {
        // OpenGL specific drawing code for Square
    }
};
class VulkanSquareStrategy : public DrawSquareStrategy {
public:
    void draw(Square const& s) override {
        // Vulkan specific drawing code for Square
    }
};
```
Now, you may wonder why we are being so explicit about the strategies here. Why use `DrawCircleStrategy` instead of just having a `DrawStrategy` with function overloading for each Shape? The base reason is you would just be undoing all the benefits of this design pattern. We are trying to break dependencies but due to this generic base class, we are once again coupling different strategies of different shapes together. With generic `DrawStrategy`, the `Circle` class would store strategy for `Square` and `Triangle` as well which is very undesirable. 

### Strategy Pattern beyond OOP

So, yeah! This solution actually works! In fact, this design pattern is much more prominent in the standard library itself than you might think. Also, this design pattern as stated before is not just limited to Object-Oriented Programming. Here are a few examples of this pattern in the standard library:
1. `std::sort(nums.begin(), nums.end(), comparator);` - Here, the `comparator` is a strategy that defines how to compare two elements.
2. `std::accumulate(nums.begin(), nums.end(), 0, binary_op);` - Here, the `binary_op` is a strategy that defines how to accumulate the elements.
3. `template<class T, class Alloc> class vector;` - Here, the `Alloc` is a strategy that defines how to allocate memory for the vector.
4. `template<class T, class Compare> class set;` - Here, the `Compare` is a strategy that defines how to compare the elements in the set.
5. `template<class Key, class T, class Hash, class KeyEqual> class unordered_map;` - Here, the `Hash` and `KeyEqual` are strategies that define how to hash the keys and compare the keys for equality respectively.
6. `template<class T, class Deleter> class unique_ptr;` - Here, the `Deleter` is a strategy that defines how to delete the managed object.

Thus, we can see that the Strategy Pattern is widely used in the standard library itself. It is a powerful design pattern that can help us achieve polymorphism without the drawbacks of inheritance and virtual functions. Notice how `std::vector` and `std::sort` don't force the user to use pointers. The Comparator and Allocator are passed by value.

### Are we done?
Great! We have successfully designed a system that can store different shapes in a single container and call their `draw()` methods without knowing the implementation details, created opportunities for easy extension of functionalities without increasing the depth of class hierarchies and avoided code duplication. However, this design is still not perfect. 
- The Strategy Pattern decoupled our logic, but it also forced us into this "Pointer World". As shown in the example snippet below, we have to manually manage lifetimes using `std::unique_ptr`. We have to deal with manual tiny allocations and consider ownership semantics.
```cpp
int main() {
    std::vector<std::unique_ptr<Shape>> shapes;
    shapes.push_back(std::make_unique<Circle>(5.0, std::make_unique<OpenGLCircleStrategy>()));
    shapes.push_back(std::make_unique<Square>(4.0, std::make_unique<VulkanSquareStrategy>()));
    for (const auto& shape : shapes) {
        shape->draw();
    }
    return 0;
}
```
- We also have double the indirections here - one for the shape and another for the strategy. This leads to performance overhead. Not good!
- Also, for each new functionality, we need to create a new base class for the strategy. This can lead to a proliferation of base classes which can be hard to manage.
- Notice how difficult it is to create a copy of an existing shape like `Circle`. You need to copy not only the phyiscal attributes like `radius` but also the strategy which are stored as `unique_ptr`. This can lead to a lot of boilerplate code and complexity. What we want from our shapes is to behave like value types, for example `int`- easy to copy, easy to move, easy to store- but still have polymorphic behavior! Thus, our solution is far from perfect.

What we actually want from our solution is the ability to extend along 3 axes independently:
1. Adding new shapes like Triangle, Rectangle etc.
2. Adding new functionalities like serialize(), transform() etc.
3. Adding new implementations for existing functionalities like OpenGL, Vulkan etc.

Extension along any of these axes should not affect the other axes. This is where Type Erasure comes into play.
## A Better Solution: Type Erasure
Some of the readers may have heard of the term "Type Erasure" before but for the sake of completeness, let's define it here. Firstly, let's be very clear what "type erasure" is NOT!
- Type erasure is **NOT** just a `void*`;
- Type erasure is **NOT** `pointer-to-base-class`;
- Type erasure is **NOT** `std::variant`; In fact, it is quite the opposite! `std::variant` is a closed set of types with open set of operations whereas type erasure is an open set of types with closed set of operations.

This is the solution used in the standard library for `std::function`. `std::function` can store any callable type like function pointers, lambda expressions, bind expressions etc. and provide a uniform interface to call them. This is achieved using type erasure.

### What is Type Erasure then?
Type erasure is:
- a templated constructor plus
- a completely non-virtual interface while hiding virtual dispatch behind a stable boundary;
- a combination of 3 clever design patterns:
    1. **External Polymorphism**
    2. **Bridge Pattern**
    3. **Prototype Pattern**

We will get to these patterns later when they appear in the solution.

### Applying Type Erasure to our Problem
Let's begin with our solution. We will first create geometric primitives for the shapes we need. These will be simple classes with no knowledge of drawing or anything that can be done on them and thus are very independent of each other and don't know about each other's existence! Thus, we don't need to couple them together using a base class.
```cpp
class Circle {
private:
    double radius;
public:
    Circle(double r) : radius(r) {}
    double getRadius() const { return radius; }
};
class Square {
private:
    double side;
public:
    Square(double s) : side(s) {}
    double getSide() const { return side; }
};
```
Next, we create two structs as below. First, we create a base class `ShapeConcept` that defines the interface for the operations we want to perform on the shapes. This class has pure virtual functions for each operation like `draw()`, `serialize()` etc. Next, we create a templated derived class ShapeModel that implements the `ShapeConcept` interface for a specific shape type `T`. This class stores an object of type `T` which is one of our shape(`Circle`, `Square`, etc.) and implements the operations. What we are doing here is allowing libraries to implement their operations on our geometric primitives using free functions(functions not bound to any class). Why free functions? They grant much more flexibility than other types of functions. So, now changing the library implementation is as simple as changing the include files! If we want to use `OpenGL`, we include the `OpenGL` header files! If we want to use `Vulkan`, we include the `Vulkan` header files!
```cpp
struct ShapeConcept {
    virtual ~ShapeConcept() = default;
    virtual void draw(/* ... */) const = 0;
    virtual void serialize(/* ... */) const = 0;
    // ...
};
template <typename T>
struct ShapeModel : ShapeConcept {
    T object;
    ShapeModel(T obj) : object{std::move(obj)} {}
    void draw(/* ... */) const override {
        // Call the appropriate draw function based on the type of T
        draw(object /*, ... */);
    }
    void serialize(/* ... */) const override {
        // Call the appropriate serialize function based on the type of T
        serialize(object /*, ... */);
    }
};
```
This is our first design pattern - **External Polymorphism**. The intent of this design pattern is to make the behvaiour vary independently of the object’s type, without requiring object to participate in the polymorphism. The objects(`Circle`, `Square`, etc.) are just dumb data holders while `ShapeModel` provides the polymorphic behavior by implementing the operations using free functions. This reduces us the cost of double indirections and also frees the two axes of extension from each other. Now, we can add new shapes without affecting the operations and vice-versa. Let’s see how we can use these classes:

```cpp
int main() {
    std::vector<std::unique_ptr<ShapeConcept>> shapes;
    shapes.push_back(std::make_unique<ShapeModel<Circle>>(Circle{5.0}));
    shapes.push_back(std::make_unique<ShapeModel<Square>>(Square{4.0}));
    for (const auto& shape : shapes) {
        shape->draw(/* ... */);
    }
    return 0;
}
```
Even after this solution, we still have a lot of manual small allocations and a lot of play with pointers. So, let’s just wrap this all in a single class and make it manage the memory for us. Why wrap? Because we want to provide a clean interface to the user and hide the implementation details.
```cpp
class Shape {
private:
  struct ShapeConcept {
    virtual ~ShapeConcept() = default;
    virtual void draw(/* ... */) const = 0;
    virtual void serialize(/* ... */) const = 0;
    // ...
    };
    template <typename T>
    struct ShapeModel : ShapeConcept {
        T object;
        ShapeModel(T obj) : object(std::move(obj)) {}
        void draw(/* ... */) const override {
            // Call the appropriate draw function based on the type of T
            draw(object /*, ... */);
        }
        void serialize(/* ... */) const override {
            // Call the appropriate serialize function based on the type of T
            serialize(object /*, ... */);
        }
    };
    friend void draw(const Shape& shape /*, ... */) {
        shape.shapePtr->draw(/* ... */);
    }
    friend void serialize(const Shape& shape /*, ... */) {
        shape.shapePtr->serialize(/* ... */);
    }
    std::unique_ptr<ShapeConcept> shapePtr;
public:
    template <typename T>
    Shape(T obj) : shapePtr{ShapeModel<T>{std::move(obj)}} {}
};
```
The templated constructor here allows the `Shape` class to accept any shape type and create the appropriate `ShapeModel` for it, store it and then just forget about it. The user of the `Shape` class does not need to worry about the memory management or the details of the shape type. 

Structurally, this is our second design pattern - **Bridge Pattern**. The Bridge Pattern is a structural design pattern that decouples an abstraction from its implementation so that the two can vary independently. In this case, the `Shape` class is the abstraction and the `ShapeConcept` is the implementor interface and `ShapeModel<T>` classes are the concrete implementations. This allows abstraction (`Shape`) and implementations (`ShapeModel<T>`) to vary independently. New concrete types can be supported without modifying `Shape`, and the changes to `Shape` do not affect the concrete types.

Notice, we also have friend functions `draw()` and `serialize()` that call the corresponding methods on the `ShapeConcept`. This allows us to call these functions on the `Shape` class without exposing the implementation details. 

Finally, to make `Shape` truly act like a value type, we need to implement copy and move semantics. Luckily, we don't need to do much for the special member functions for move and can default them. However, the case for copy is a bit tricky. How do we copy something without knowing its type? Let's create a `clone()` method in the `ShapeConcept` that returns a `std::unique_ptr<ShapeConcept>`.
```cpp
class Shape {
private:
    struct ShapeConcept {
        virtual ~ShapeConcept() = default;
        virtual void draw(/* ... */) const = 0;
        virtual void serialize(/* ... */) const = 0;
        virtual std::unique_ptr<ShapeConcept> clone() const = 0;
    };
    template <typename T>
    struct ShapeModel : ShapeConcept {
        T object;
        ShapeModel(T obj) : object(std::move(obj)) {}
        void draw(/* ... */) const override {
            draw(object /*, ... */);
        }
        void serialize(/* ... */) const override {
            serialize(object /*, ... */);
        }
        std::unique_ptr<ShapeConcept> clone() const override {
            return std::make_unique<ShapeModel<T>>(*this);
        }
    };
    //...
};
```
This is our third design pattern - **Prototype Pattern**. The Prototype Pattern is a creational design pattern that allows cloning of objects without knowing their concrete types. Here, the `clone()` method in the `ShapeConcept` allows us to create a copy of the object without knowing its type. Now, we can implement the copy constructor and copy assignment operator for the `Shape` class quite easily. Let's see the usage of the final `Shape` class:
```cpp
int main() {
    std::vector<Shape> shapes;
    shapes.emplace_back(Circle{5.0});
    shapes.emplace_back(Square{4.0});
    for (const auto& shape : shapes) {
        draw(shape /*, ... */);
    }
    return 0;
}
```
Thus, we have successfully designed a system that can store different shapes in a single container and call their `draw()` methods without knowing the implementation details, created opportunities for easy extension of functionalities without increasing the depth of class hierarchies and avoided code duplication - all while avoiding the drawbacks of inheritance and virtual functions!

### A safer templated constructor
One issue with the above implementation is that the templated constructor can accept any type, even those that do not have the required operations like `draw()` and `serialize()`. This can lead to compilation errors that are hard to understand. To avoid this, we can use `concepts` to constrain the types that can be passed to the templated constructor. Let's define a concept `Drawable` that checks if a type has the required operations. Similarly, we can define a concept `Serializable` for the `serialize()` operation. Finally, we can define a concept `Shapelike` that combines both `Drawable` and `Serializable`. We can then use this concept to constrain the templated constructor of the `Shape` class.
```cpp
template <typename T>
concept Drawable = requires(T obj) {
    { draw(obj /*, ... */) } -> std::same_as<void>;
};

template <typename T>
concept Serializable = requires(T obj) {
    { serialize(obj /*, ... */) } -> std::same_as<void>;
};

template <typename T>
concept Shapelike = Drawable<T> && Serializable<T>;
class Shape {
private:
    //...
public:
    template <typename T> 
                    requires Shapelike<T>
    Shape(T obj) : shapePtr{std::make_unique<ShapeModel<T>>(std::move(obj))} {}
    //...
};
```
### Injecting strategies
One limitation of the above implementation is that the operations like `draw()` and `serialize()` are fixed. We can only use one library implementation in a single file. Well, to change this, let's just add another template parameter to the `ShapeModel` class that represents the strategy for the operations. Now, we won't modify the `ShapeModel` class but create another class `ExtendedShapeModel` that inherits from `ShapeConcept`.
```cpp
class Shape {
private:
    struct ShapeConcept {
        //...
    };
    template <typename T, typename DrawStrategy>
    struct ExtendedShapeModel : ShapeConcept {
        T object;
        DrawStrategy drawStrategy;
        //...
        ExtendedShapeModel(T obj, DrawStrategy ds) 
            : object{std::move(obj)}, drawStrategy{std::move(ds)} {}
        void draw(/* ... */) const override {
            drawStrategy.draw(object /*, ... */);
        }
        //...
    };
    //...
public:
    template<typename T, typename DrawStrategy>
                    requires Shapelike<T> && DrawableStrategy<DrawStrategy, T> // assume DrawableStrategy concept is defined
    Shape(T obj, DrawStrategy ds) 
        : shapePtr{std::make_unique<DrawableShapeModel<T, DrawStrategy>>(std::move(obj), std::move(ds))} {}
    //...
};
```
Here, the `ExtendedShapeModel` class takes an additional template parameter `DrawStrategy` which represents the strategy for the `draw()` operation. We can directly use this `ExtendedShapeModel` class instead of our previous `ShapeModel` class with some default strategy for `draw()`. This allows the users to inject the strategy they want to use for the operations. Similarly, we can add template parameters for other operations like `serialize()` as well. Now, the user can create a `Shape` object with a specific strategy for the operations like this:
```cpp
int main() {
    OpenGLDrawStrategy openglDrawStrategy;
    VulkanDrawStrategy vulkanDrawStrategy;
    Shape circleShape(Circle{5.0}, openglDrawStrategy);
    Shape squareShape(Square{4.0}, vulkanDrawStrategy);
    Shape testcircle(Circle{3.0}, [/* lambda draw strategy */](const Circle& c /*, ... */) {
        // Custom drawing code for Circle
    });
    circleShape.draw(/* ... */);
    squareShape.draw(/* ... */);
    return 0;
}
```

#### Have we come full circle?
Well, not really! We can actually use the template-based Strategy Pattern and it can work to some extent but we are currently at much better place to be using template-based strategy pattern. If we would have tried to do so earlier, we would fail to reason about the design. By our design, we would fail to conclude that `Circle<OpenGLDrawStrategy>` and `Circle<VulkanDrawStrategy>` that differ only in strategy are same. They will be treated as fundamentally different types! Also, not to forget, the value semantics are still missing with this approach. But with type erasure, we have a clean separation of concerns and can reason about the design much better. The public API is much cleaner, easier to use and stable while we have separated the more volatile aspects- the implementation details - behind a stable boundary. This allows us to change the implementation details without affecting the public API.

### Conclusion
![type-erase-sol](../images/type-erasure/type-erase-sol.png)
As we can see in the above architecture diagram, the `Shape` class acts as a wrapper and abstraction for the different shape types. It contains a pointer to the `ShapeConcept` which defines the interface for the operations that can be performed on the shapes. 

One level below this, we have all the different kinds of shapes like `Circle`, `Square` etc. These are totally independent of each other, don't know about each other's existence, don't know what operations can be performed on them and have zero knowledge about the level above them and below them. Now, these can be presented at any level but for the sake of clarity, we put them here. 

Now, we need to combine all of these. This happens with `draw()` implementation. This happens with the help of templated constructor of `ShapeModel` which acts as a bridge between the two levels and we don't need to create those classes for each shape as the compiler itself generates them for us. Thus, we have very loose coupling between the different levels. Just to say, I have omitted the templated strategy implementation here for the sake of clarity. Keep in mind that the `ShapeModel` class can be replaced with `ExtendedShapeModel` class for strategy injection.

But do note that there are a few trade-offs, not bad but trade-offs nonetheless:
1. **Harder to debug:** When something goes wrong with `draw(Shape)`, we no longer know what you're drawing without instrumentation. We have traded **structural transparency** for **architectural flexibility**.
2. **Misleading error messages:** Without `concepts`, the error messages become unreadable fast.
3. **Optimization visibility:** Inlining across erased boundaries is harder. Sometimes, compile may win. Sometimes, it won't.
4. **Semantic opacity:** With inheritance model, behaviour is discoverable via type graph. With type erasure, behaviour is discoverable via construction site. This shifts the cognitive load from "what is this object?" to "how was this object constructed?". Again, not bad but different.

In this blog, we have concentrated on design perspective and there is no focus on performance whatsoever. Our entire focus is on creating a flexible and maintainable design. 
Hence, the performance may not be any better than classic object oriented approach! But it is not the end of line. We still have significant opportunities for doing some clever optimizations to increase performance. For example, we can use `small buffer optimization` to avoid heap allocations for small objects. These optimizations are out of the scope of this blog but I would recommend the readers to look into them.
## References
1. [Design Patterns: Elements of Reusable Object-Oriented Software](https://www.amazon.com/Design-Patterns-Elements-Reusable-Object-Oriented/dp/0201633612) by Erich Gamma, Richard Helm, Ralph Johnson, John Vlissides
2. [Breaking Dependencies: Type Erasure - A Design Analysis](https://youtu.be/4eeESJQk-mw?si=aOFHcYz2ygJMNv0r)
3. [Breaking Dependencies: Type Erasure - The Implementation Details](https://www.youtube.com/watch?v=qn6OqefuH08)