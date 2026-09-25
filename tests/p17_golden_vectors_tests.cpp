#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <vita/codec/packet.hpp>
#include <vita/profiles/iq/sdr.hpp>
#include <vita/profiles/iq/profile.hpp>
#include <vita/runtime/context/publisher.hpp>
#include <vita/runtime/transaction/outcomes.hpp>

using namespace vita;
using namespace vita::codec;
using namespace vita::profiles::iq;

namespace {

struct GoldenVector {
  std::string name;
  std::size_t expected_size = 0;
  std::string sha256;
  std::vector<std::byte> bytes;
};

[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error(message);
}

void require(bool condition, const std::string &message) {
  if (!condition) {
    fail(message);
  }
}

unsigned nibble(char value) {
  if (value >= '0' && value <= '9') {
    return static_cast<unsigned>(value - '0');
  }
  if (value >= 'a' && value <= 'f') {
    return static_cast<unsigned>(value - 'a' + 10);
  }
  fail("invalid hexadecimal digit");
}

void append_hex(std::vector<std::byte> &output, std::string_view hex,
                std::size_t repetitions) {
  require(hex.size() % 2 == 0, "odd hexadecimal segment length");
  std::vector<std::byte> segment;
  segment.reserve(hex.size() / 2);
  for (std::size_t offset = 0; offset < hex.size(); offset += 2) {
    segment.push_back(
        std::byte((nibble(hex[offset]) << 4) | nibble(hex[offset + 1])));
  }
  for (std::size_t count = 0; count < repetitions; ++count) {
    output.insert(output.end(), segment.begin(), segment.end());
  }
}

std::vector<std::string_view> split(std::string_view input, char delimiter) {
  std::vector<std::string_view> fields;
  while (true) {
    const auto next = input.find(delimiter);
    fields.push_back(input.substr(0, next));
    if (next == std::string_view::npos) {
      return fields;
    }
    input.remove_prefix(next + 1);
  }
}

std::vector<GoldenVector> load_vectors() {
  std::ifstream input(P17_GOLDEN_FILE);
  require(input.good(), "cannot open golden-vector corpus");
  std::vector<GoldenVector> vectors;
  for (std::string line; std::getline(input, line);) {
    if (line.empty() || line.front() == '#') {
      continue;
    }
    const auto columns = split(line, '|');
    require(columns.size() == 4, "invalid golden-vector record");
    GoldenVector vector;
    vector.name = columns[0];
    vector.expected_size = std::stoull(std::string(columns[1]));
    vector.sha256 = columns[2];
    require(vector.sha256.size() == 64, "invalid SHA-256 field");
    for (const auto encoded : split(columns[3], ',')) {
      const auto marker = encoded.find('*');
      const auto hex = encoded.substr(0, marker);
      const auto repetitions = marker == std::string_view::npos
                                   ? 1
                                   : std::stoull(std::string(
                                         encoded.substr(marker + 1)));
      append_hex(vector.bytes, hex, repetitions);
    }
    require(vector.bytes.size() == vector.expected_size,
            vector.name + ": decoded length mismatch");
    vectors.push_back(std::move(vector));
  }
  return vectors;
}

std::optional<RequestContext> request_context(std::string_view name) {
  if (name == "execution-ack" || name == "status-ack" ||
      name == "diagnostic-ack") {
    return RequestContext{0xa11f0000};
  }
  if (name == "capability-response") {
    return RequestContext{0xa0040000};
  }
  return std::nullopt;
}

template <class Builder>
void configure(Builder &builder, const PacketView &packet) {
  const auto &envelope = packet.envelope.envelope;
  const auto packet_class = envelope.class_id ? envelope.class_id->packet_class : 0;
  const auto action = envelope.command
                          ? static_cast<std::uint8_t>(envelope.command->cam >> 23 & 3)
                          : 0;
  require(static_cast<bool>(builder.configure(packet_class, action)),
          "cannot configure packet builder");
}

std::size_t encode_values(const PacketView &packet, MutableBytes output) {
  const auto subtype = packet.envelope.envelope.type == PacketType::context
                           ? PacketSubtype::context
                           : packet.envelope.envelope.ack
                                 ? PacketSubtype::state_ack
                                 : PacketSubtype::control;
  if (subtype == PacketSubtype::context) {
    ContextPacket builder;
    configure(builder, packet);
    for (std::size_t index = 0; index < packet.fields.size(); ++index) {
      const auto &field = packet.fields[index];
      require(static_cast<bool>(field.materialize_into(builder)),
              "cannot materialize context field");
    }
    const auto encoded = encode_packet(packet.envelope.envelope, builder.freeze(),
                                       output, packet.change);
    require(static_cast<bool>(encoded), "cannot encode context packet");
    return *encoded;
  }
  if (subtype == PacketSubtype::state_ack) {
    StateAck builder;
    configure(builder, packet);
    for (std::size_t index = 0; index < packet.fields.size(); ++index) {
      const auto &field = packet.fields[index];
      require(static_cast<bool>(field.materialize_into(builder)),
              "cannot materialize state field");
    }
    const auto encoded =
        encode_packet(packet.envelope.envelope, builder.freeze(), output);
    require(static_cast<bool>(encoded), "cannot encode state acknowledgment");
    return *encoded;
  }
  ControlPacket builder;
  configure(builder, packet);
  for (std::size_t index = 0; index < packet.fields.size(); ++index) {
    const auto &field = packet.fields[index];
    require(static_cast<bool>(field.materialize_into(builder)),
            "cannot materialize control field");
  }
  const auto encoded = encode_packet(packet.envelope.envelope, builder.freeze(),
                                     output, packet.change);
  require(static_cast<bool>(encoded), "cannot encode control packet");
  return *encoded;
}

std::size_t encode_query(const PacketView &packet, MutableBytes output) {
  QueryPacket builder;
  configure(builder, packet);
  std::uint32_t attributes = 0;
  for (std::size_t index = 0; index < packet.fields.size(); ++index) {
    const auto &field = packet.fields[index];
    bool selected = false;
    for (const auto &existing : builder.freeze().fields()) {
      selected |= existing.id == field.id;
    }
    if (!selected) {
      require(static_cast<bool>(builder.select(field.id)),
              "cannot select query field");
    }
    attributes |= attribute_bit(field.attribute);
  }
  if (attributes != attribute_bit(Attribute::current)) {
    require(static_cast<bool>(builder.with_attributes(attributes)),
            "cannot configure query attributes");
  }
  const auto encoded =
      encode_packet(packet.envelope.envelope, builder.freeze(), output);
  require(static_cast<bool>(encoded), "cannot encode query packet");
  return *encoded;
}

std::size_t encode_diagnostics(const PacketView &packet, MutableBytes output) {
  DiagnosticAck warnings;
  DiagnosticAck errors;
  configure(warnings, packet);
  configure(errors, packet);
  for (std::size_t index = 0; index < packet.fields.size(); ++index) {
    const auto &field = packet.fields[index];
    auto &builder = field.group == DiagnosticGroup::warning ? warnings : errors;
    require(static_cast<bool>(builder.diagnostic(field.id, *field.diagnostic())),
            "cannot materialize diagnostic field");
  }
  const auto encoded = encode_diagnostic(packet.envelope.envelope,
                                         warnings.freeze(), errors.freeze(),
                                         RequestContext{0xa11f0000}, output);
  require(static_cast<bool>(encoded), "cannot encode diagnostic acknowledgment");
  return *encoded;
}

std::size_t encode_capabilities(const PacketView &query, MutableBytes output) {
  SdrCapabilities supported;
  runtime::transaction::AckRecord ranges;
  ranges.request = query.envelope.envelope;
  ranges.cam = *runtime::transaction::Cam::parse(
      ranges.request, runtime::transaction::Profile::sdr_radio);
  ranges.kind = runtime::transaction::AckKind::state;
  ranges.selected_mask = 0x72;
  ranges.sdr_capabilities = &supported;
  ranges.time_known = true;
  ranges.epoch = Tsi::utc;
  ranges.time = {1000, 0};
  ranges.scheduled_or_executed = true;
  const auto encoded = runtime::transaction::encode_response(ranges, output);
  require(static_cast<bool>(encoded), "cannot encode capability response");
  return *encoded;
}

void compare(const GoldenVector &vector,
             const std::optional<PacketView> &capability_query) {
  const auto request = request_context(vector.name);
  const auto decoded = decode_packet(vector.bytes, DecodeOptions{request});
  require(static_cast<bool>(decoded), vector.name + ": migrated decode failed");
  std::vector<std::byte> actual(vector.bytes.size() + 64);
  std::size_t size = 0;
  if (is_data(decoded->envelope.envelope.type)) {
    const auto encoded = encode_envelope(decoded->envelope.envelope,
                                         decoded->envelope.payload,
                                         decoded->envelope.trailer, actual);
    require(static_cast<bool>(encoded), vector.name + ": signal encode failed");
    size = *encoded;
  } else if (vector.name == "capability-response") {
    require(capability_query.has_value(), "capability query not loaded");
    size = encode_capabilities(*capability_query, actual);
  } else if (decoded->body_kind == BodyKind::selectors) {
    size = encode_query(*decoded, actual);
  } else if (decoded->body_kind == BodyKind::diagnostics) {
    size = encode_diagnostics(*decoded, actual);
  } else {
    size = encode_values(*decoded, actual);
  }
  require(size == vector.bytes.size(), vector.name + ": encoded length mismatch");
  require(std::equal(vector.bytes.begin(), vector.bytes.end(), actual.begin()),
          vector.name + ": migrated bytes differ from pre-rename golden vector");
}

} // namespace

int main() {
  try {
    const auto vectors = load_vectors();
    constexpr std::array<std::string_view, 14> expected_names{
        "signal-single",      "signal-first",       "signal-middle",
        "signal-final",       "context-current",    "configure",
        "start-scheduled",    "stop",               "status-query",
        "execution-ack",      "status-ack",         "diagnostic-ack",
        "capability-query",   "capability-response"};
    require(vectors.size() == expected_names.size(),
            "golden-vector inventory count mismatch");
    std::optional<PacketView> capability_query;
    for (std::size_t index = 0; index < vectors.size(); ++index) {
      require(vectors[index].name == expected_names[index],
              "golden-vector inventory order/name mismatch");
      if (vectors[index].name == "capability-query") {
        auto decoded = decode_packet(vectors[index].bytes);
        require(static_cast<bool>(decoded), "capability query decode failed");
        capability_query = *decoded;
      }
      compare(vectors[index], capability_query);
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
