#include <memory>
#include <unordered_map>
#include <vector>
#include <cassert>
#include <iostream>

class GateKeeper;

/** A gate is a one-output zero-input simple gate. There are exactly three types: Nand, LowOutput and Register, and I/O.
 * The idea is that every digital circuit can be created using these elements... So I had to try */
class IGate {
public:
    virtual void tick1() {};
    virtual void tick2() {};
    virtual bool getValue() const=0;
    virtual int getNumInputs() const=0;
    virtual IGate*& getInput(int i)=0;
    virtual IGate* getInput(int i) const=0;
    virtual ~IGate() {}
    virtual std::string getType() const=0;
};

/** builds a long name used by mostly in prototype to generate names to the gates */
class LongNameBuilder {
    std::string name;
public:
    void addChildId(std::string v) {
        name += "{";
        name += v;
        name += "}: ";
    }
    void addType(std::string v) {
        name += "[";
        name += v;
        name += "] ";
    }
    const std::string& getName() const { return name; }
};

/** stores all the gates in a circuit, manages its' lifetimes */
class GateKeeper {
    std::vector<std::pair<std::string, std::unique_ptr<IGate>>> gates;
public:
    void addGate(LongNameBuilder name, std::unique_ptr<IGate> gate) {
        gates.push_back({name.getName(), std::move(gate)});
    }
    void tick() {
        for (auto& c: gates) c.second->tick1();
        for (auto& c: gates) c.second->tick2();
    }
    void print() const {
        for (auto& i: gates) {
            std::cout << i.first << std::endl;
        }
    }
};

/** A base gate which manages its inputs. */
template<int N>
class Gate : public IGate {
    std::array<IGate*,N> inputs;
public:
    static constexpr int InputSize = N;
    IGate*& getInput(int i) override { return inputs.at(i); }
    IGate* getInput(int i) const override { return inputs.at(i); }
    Gate(const Gate&)=delete;
    Gate& operator=(const Gate&)=delete;

    int getNumInputs() const override { return N; }

    Gate() : IGate() { }
    virtual ~Gate() {}
};

/** Returns low to the circuit connecting to it */
class LowOutput : public Gate<0> {
public:
    bool getValue() const override { return false; }
    std::string getType() const override { return "low"; }
};

/** A simple register, or repeater: always returns the last tick's value */
class Register : public Gate<1> {
    bool value=false;
    bool nextValue = false;
public:
    std::string getType() const override { return "register"; }
    void tick1() override { nextValue = getInput(0)->getValue(); }
    void tick2() override { value = nextValue; }
    bool getValue() const {
        return value;
    }
};

/** A nand gate: Not(And(A,B)) */
class Nand : public Gate<2> {
public:
    std::string getType() const override { return "nand"; }
    bool getValue() const override {
        return !(getInput(0)->getValue() && getInput(1)->getValue());
    }
};

/** shows its value on every tick */
class TickOutputOnly : public Gate<1> {
    int t=0;
    const std::string name;
public:
    TickOutputOnly(std::string name) : Gate(), name(std::move(name)) {}
    std::string getType() const override { return "tick - outputonly"; }

    void tick1() override {
        std::cout << name.c_str() << ": tick" << ++t << ": " << (getInput(0)->getValue() ? 'H' : 'L') << std::endl;
    }
    bool getValue() const override {
        assert(false);
        return false; // TODO
    }
};

/** changeable input */
class Input : public Gate<2> {
    // TODO Input gates cannot be found, and, as such, they cannot be changed in a circuit
    bool val=false;
    std::string name;
public:
    Input(std::string name) : Gate(), name(std::move(name)) { }
    std::string getType() const override { return "user-input"; }
    void setValue(bool newVal) {
        val = newVal;
    }
    bool getValue() const override {
        return val;
    }
};

/** a circuit, storing big chunks of gates */
class ICircuit {
public:
    virtual IGate* getOutput(int i)=0;
    virtual void link(const std::vector<IGate*>& args)=0;
    virtual ~ICircuit() {}
};

/** a circuit from a gate */
template<typename T>
class GateCircuit : public ICircuit {
    IGate* c ;
public:
    template<typename... Args>
    GateCircuit(GateKeeper* heimdall, const LongNameBuilder& builder, Args&&... args) {
        auto cc = std::make_unique<T>(std::forward<Args>(args)...);
        c = cc.get();
        LongNameBuilder builder2 = builder;
        builder2.addType(c->getType());
        heimdall->addGate(builder2, std::move(cc));
    }
    IGate* getOutput(int i) override {
        assert(i == 0);
        return c;
    }
    void link(const std::vector<IGate*>& args) override {
        assert((int)args.size() == c->getNumInputs());
        for (int i = 0; i < (int)args.size(); i++)
            c->getInput(i) = args[i];
    }
};

/** A prototype storing information on how to link gates. Creates circuits on instantiation call */
class IPrototype {
    const int numInputs, numOutputs;
public:
    IPrototype(int in, int out) : numInputs(in), numOutputs(out) {}
    virtual std::unique_ptr<ICircuit> instantiate(GateKeeper*, const LongNameBuilder&) const=0;
    int getNumInputs() const { return numInputs; }
    int getNumOutputs() const { return numOutputs; }
    virtual ~IPrototype() {}
};

/** A prototype for a simple gate */
template<typename T, int N = T::InputSize>
class GatePrototype : public IPrototype {
    GatePrototype() : IPrototype(N, 1) {}
public:
    std::unique_ptr<ICircuit> instantiate(GateKeeper* heimdall, const LongNameBuilder& builder) const override {
        return std::make_unique<GateCircuit<T>>(heimdall, builder);
    }
    inline static GatePrototype* getInstance() {
        static GatePrototype instance;
        return &instance;
    }
};

/** Aliases for easier use */
using NandPrototype = GatePrototype<Nand>;
using LowOutputPrototype = GatePrototype<LowOutput>;
using RegisterPrototype = GatePrototype<Register>;

/** A prototype for a TickOutputOnly gate */
class OutputPrototype : public IPrototype {
    const std::string name;
public:
    OutputPrototype(std::string name) : IPrototype(1,0), name(name) {}
    std::unique_ptr<ICircuit> instantiate(GateKeeper* heimdall, const LongNameBuilder& builder) const override {
        return std::make_unique<GateCircuit<TickOutputOnly>>(heimdall, builder, name);
    }
};

/** Stores the information of how to build a bigger circuit from a smaller one. */
class CompositePrototype : public IPrototype {

    struct ChildData {
        const IPrototype* const proto;
        const std::vector<std::string> inputs;
        const std::vector<std::string> outputs;
        const std::string child_id;
    };
    std::vector<ChildData> commands;
    enum { Init, Finalized } state;
    const std::vector<std::string> outer_input_ids;
    const std::vector<std::string> outer_output_ids;
    int num_nodes = -1;
    const std::string type_name;

    class Circuit : public ICircuit {
        enum { Init, Linked } state;
        std::unordered_map<std::string, IGate*> everything;
        std::vector<std::unique_ptr<ICircuit>> circuits;
        const CompositePrototype* const parent;
    public:
        Circuit(GateKeeper* heimdall, const LongNameBuilder& builder, const CompositePrototype* parent) : parent(parent) {
            state = Init;
            for (auto& cmd : parent->commands) {
                auto x = cmd.proto;
                auto& outputs = cmd.outputs;
                auto& childId = cmd.child_id;
                LongNameBuilder builder2 = builder;
                builder2.addType(parent->type_name);
                if (childId != "")
                    builder2.addChildId(childId);
                std::unique_ptr<ICircuit> circuit = x->instantiate(heimdall, builder2);
                for (int i = 0; i < x->getNumOutputs(); i++) {
                    assert(everything.find(outputs[i]) == everything.end());
                    everything.insert({outputs[i], circuit->getOutput(i)});
                }
                circuits.push_back(std::move(circuit));
            }
        }

        IGate* getOutput(int i) override {
            assert(i >= 0 && i < parent->getNumOutputs());
            assert(everything.find(parent->outer_output_ids[i]) != everything.end());
            return everything[parent->outer_output_ids[i]];
        }

        void link(const std::vector<IGate*>& args) override {
            assert(state == Init);
            state = Linked;
            assert((int)args.size() == parent->getNumInputs());
            for (int i = 0; i < (int)args.size(); i++) {
                everything.insert({parent->outer_input_ids[i], args[i]});
            }
            for (int i = 0; i < (int)circuits.size(); i++) {
                auto& desc = parent->commands[i];
                auto& inputs = desc.inputs;
                std::vector<IGate*> data;
                for (int i = 0; i < (int)inputs.size(); i++) {
                    data.push_back(everything[inputs[i]]);
                }
                circuits[i]->link(data);
            }
        }
    };
public:
    CompositePrototype(std::string name, std::vector<std::string> outer_input_ids, std::vector<std::string> outer_output_ids)
        : IPrototype(outer_input_ids.size(), outer_output_ids.size()), state(Init),
            outer_input_ids(std::move(outer_input_ids)), outer_output_ids(std::move(outer_output_ids)), type_name(std::move(name)) {
        num_nodes = (int)this->outer_input_ids.size();
    }
    void addPrototype(const IPrototype& cmd, std::vector<std::string> input_ids = {}, std::vector<std::string> output_ids = {}, std::string childName="") {
        if (dynamic_cast<const CompositePrototype*>(&cmd)) {
            assert(dynamic_cast<const CompositePrototype*>(&cmd)->state == Finalized);
        }
        assert(cmd.getNumInputs() == (int)input_ids.size());
        assert(cmd.getNumOutputs() == (int)output_ids.size());
        assert(state == Init);
        num_nodes += (int)output_ids.size();
        commands.push_back({&cmd, input_ids, output_ids, childName});
    }
    void finalize() {
        assert(state == Init);
        state = Finalized;
    }
    std::unique_ptr<ICircuit> instantiate(GateKeeper* heimdall, const LongNameBuilder& builder=LongNameBuilder()) const override {
        return std::make_unique<Circuit>(heimdall, builder, this);
    }
};

int main() {

    IPrototype& lowPrototype = *LowOutputPrototype::getInstance();
    IPrototype& nandPrototype = *NandPrototype::getInstance();
    IPrototype& registerPrototype = *RegisterPrototype::getInstance();

    CompositePrototype notPrototype("not", {"in"},{"not"});
    notPrototype.addPrototype(nandPrototype, {"in","in"}, {"not"});
    notPrototype.finalize();

    CompositePrototype andPrototype("and", {"in1", "in2"},{"and"});
    andPrototype.addPrototype(nandPrototype, {"in1","in2"}, {"nand"});
    andPrototype.addPrototype(notPrototype, {"nand"}, {"and"});
    andPrototype.finalize();

    CompositePrototype orPrototype("or", {"in1", "in2"}, {"or"});
    orPrototype.addPrototype(notPrototype, {"in1"}, {"nin1"});
    orPrototype.addPrototype(notPrototype, {"in2"}, {"nin2"});
    orPrototype.addPrototype(nandPrototype, {"nin1","nin2"}, {"or"});
    orPrototype.finalize();

    CompositePrototype xorPrototype("xor", {"in1", "in2"}, {"xor"});
    xorPrototype.addPrototype(orPrototype, {"in1", "in2"}, {"or"});
    xorPrototype.addPrototype(nandPrototype, {"in1", "in2"}, {"nand"});
    xorPrototype.addPrototype(andPrototype, {"or", "nand"}, {"xor"});
    xorPrototype.finalize();

// new value will be always (set || data) and !reset
    CompositePrototype srFlipFlopPrototype("SR flip-flop", {"data", "set", "reset"}, {"value"});
    srFlipFlopPrototype.addPrototype(orPrototype, {"data", "set"}, {"settable"});
    srFlipFlopPrototype.addPrototype(notPrototype, {"reset"}, {"nreset"});
    srFlipFlopPrototype.addPrototype(andPrototype, {"nreset","settable"}, {"register"});
    srFlipFlopPrototype.addPrototype(registerPrototype, {"register"}, {"value"});
    srFlipFlopPrototype.finalize();

// new value is: (data nand enable) nand ((not data nand enable) nand value)
    CompositePrototype dFlipFlopPrototype("D flip-flop", {"data", "enable"}, {"value"});
    dFlipFlopPrototype.addPrototype(nandPrototype, {"data", "enable"}, {"force high"});
    dFlipFlopPrototype.addPrototype(notPrototype, {"data"}, {"not data"});
    dFlipFlopPrototype.addPrototype(nandPrototype, {"not data", "enable"}, {"force low"});
    dFlipFlopPrototype.addPrototype(nandPrototype, {"force low", "value"}, {"value with forced low"});
    dFlipFlopPrototype.addPrototype(nandPrototype, {"force high", "value with forced low"}, {"new value"});
    dFlipFlopPrototype.addPrototype(registerPrototype, {"new value"}, {"value"});
    dFlipFlopPrototype.finalize();

// 3-bit adder
    CompositePrototype adderPrototype("3-bit adder", {"1", "2", "3"}, {"value", "carry"});
    adderPrototype.addPrototype(xorPrototype, {"1", "2"}, {"1x2"});
    adderPrototype.addPrototype(xorPrototype, {"1x2", "3"}, {"value"});
    adderPrototype.addPrototype(andPrototype, {"1", "2"}, {"12"});
    adderPrototype.addPrototype(andPrototype, {"1", "3"}, {"13"});
    adderPrototype.addPrototype(andPrototype, {"3", "2"}, {"32"});
    adderPrototype.addPrototype(orPrototype, {"12", "13"}, {"12+13"});
    adderPrototype.addPrototype(orPrototype, {"12+13", "32"}, {"carry"});
    adderPrototype.finalize();

    CompositePrototype adder8Prototype("8+8 bit adder",
            {"a8", "a7", "a6", "a5", "a4", "a3", "a2", "a1", "b8", "b7", "b6", "b5", "b4", "b3", "b2", "b1"}, {"c8", "c7", "c6", "c5", "c4", "c3", "c2", "c1", "carry"});
    adder8Prototype.addPrototype(lowPrototype, {}, {"carry0"});
    adder8Prototype.addPrototype(adderPrototype, {"a1", "b1", "carry0"}, {"c1", "carry1"});
    adder8Prototype.addPrototype(adderPrototype, {"a2", "b2", "carry1"}, {"c2", "carry2"});
    adder8Prototype.addPrototype(adderPrototype, {"a3", "b3", "carry2"}, {"c3", "carry3"});
    adder8Prototype.addPrototype(adderPrototype, {"a4", "b4", "carry3"}, {"c4", "carry4"});
    adder8Prototype.addPrototype(adderPrototype, {"a5", "b5", "carry4"}, {"c5", "carry5"});
    adder8Prototype.addPrototype(adderPrototype, {"a6", "b6", "carry5"}, {"c6", "carry6"});
    adder8Prototype.addPrototype(adderPrototype, {"a7", "b7", "carry6"}, {"c7", "carry7"});
    adder8Prototype.addPrototype(adderPrototype, {"a8", "b8", "carry7"}, {"c8", "carry"});
    adder8Prototype.finalize();

    CompositePrototype clkPrototype("clock", {}, {"out"});
    clkPrototype.addPrototype(registerPrototype, {"in"}, {"out"});
    clkPrototype.addPrototype(notPrototype, {"out"}, {"in"});
    clkPrototype.finalize();

    CompositePrototype downDetectorPrototype("falling edge detector", {"clk"}, {"down"});
    downDetectorPrototype.addPrototype(registerPrototype, {"clk"}, {"old clk"});
    downDetectorPrototype.addPrototype(notPrototype, {"clk"}, {"not clk"});
    downDetectorPrototype.addPrototype(andPrototype, {"old clk", "not clk"}, {"down"});
    downDetectorPrototype.finalize();

    CompositePrototype halverPrototype("clock halver", {"clk"}, {"new current"});
    halverPrototype.addPrototype(downDetectorPrototype, {"clk"}, {"down"}, "down detector");
    halverPrototype.addPrototype(registerPrototype, {"new current"}, {"current"});
    halverPrototype.addPrototype(xorPrototype, {"current", "down"}, {"new current"}, "change on down");
    halverPrototype.finalize();

    {
        GateKeeper heimdall;
        CompositePrototype testProto("test", {}, {"out"});
        testProto.addPrototype(lowPrototype, {}, {"in1"});
        testProto.addPrototype(lowPrototype, {}, {"in2"});
        testProto.addPrototype(xorPrototype, {"in1","in2"}, {"out"});
        testProto.finalize();

        auto test = testProto.instantiate(&heimdall);
        test->link({});
        assert(!test->getOutput(0)->getValue());
    }

    {
        GateKeeper heimdall;
        CompositePrototype testProto("test", {}, {"out"});
        testProto.addPrototype(lowPrototype, {}, {"in1"});
        testProto.addPrototype(lowPrototype, {}, {"in2"});
        testProto.addPrototype(xorPrototype, {"in1","in2"}, {"out"});
        testProto.finalize();

        auto test = testProto.instantiate(&heimdall);
        test->link({});
        assert(!test->getOutput(0)->getValue());
    }

    {
        GateKeeper heimdall;
        CompositePrototype testProto("test", {}, {});
        OutputPrototype clk1("clk/1"), clk2("clk/2"), clk4("clk/4");
        testProto.addPrototype(clkPrototype, {}, {"clk"});
        testProto.addPrototype(halverPrototype, {"clk"}, {"clk/2"});
        testProto.addPrototype(clk1, {"clk"}, {});
        testProto.addPrototype(halverPrototype, {"clk/2"}, {"clk/4"});
        testProto.addPrototype(clk2, {"clk/2"}, {});
        testProto.addPrototype(clk4, {"clk/4"}, {});
        testProto.finalize();

        auto test = testProto.instantiate(&heimdall);
        test->link({});

        for (int i = 0; i < 24; i++)
            heimdall.tick(),std::cout << std::endl;
    }
    {
        GateKeeper heimdall;
        {
            CompositePrototype testProto("test", {}, {});
            OutputPrototype clk1("clk/1"), clk2("clk/2"), clk4("clk/4"), sum("sum"), carry("carry");
            testProto.addPrototype(clkPrototype, {}, {"clk/1"}, "clock");
            testProto.addPrototype(halverPrototype, {"clk/1"}, {"clk/2"}, "first halver");
            testProto.addPrototype(clk1, {"clk/1"}, {}, "first input");
            testProto.addPrototype(halverPrototype, {"clk/2"}, {"clk/4"}, "second halver");
            testProto.addPrototype(clk2, {"clk/2"}, {}, "second input");
            testProto.addPrototype(clk4, {"clk/4"}, {}, "third input");
            testProto.addPrototype(adderPrototype, {"clk/1", "clk/2", "clk/4"}, {"out", "carry"}, "adder SUT");
            testProto.addPrototype(sum, {"out"}, {}, "output");
            testProto.addPrototype(carry, {"carry"}, {}, "carry");
            testProto.finalize();

            auto test = testProto.instantiate(&heimdall);
            test->link({});
            heimdall.print();
        }

        for (int i = 0; i < 24; i++)
            heimdall.tick(),std::cout << std::endl;
    }
}
