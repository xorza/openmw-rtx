#pragma once

namespace MWMechanics
{
    template <class Base>
    class DerivedClassStorage;

    struct AiTemporaryBase;

    /// \brief Container for AI package status.
    using AiState = DerivedClassStorage<AiTemporaryBase>;
}
