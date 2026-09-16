#pragma once

class StateReader;
class StateWriter;

namespace ktp_mobile {
struct State;
}

namespace ktp_mobile_f_module_state_io {

void Read(StateReader& reader, ktp_mobile::State& state);
void Write(StateWriter& writer, const ktp_mobile::State& state);

}
