#include "./meshElement.h"

#include <RmlUi/Core/ElementUtilities.h>

#include "../meshBuilder/meshBuilder.h"

void MeshElement::markDirty() {
  this->isDirty = true;
}

void MeshElement::OnResize() {
  this->isDirty = true;
}

void MeshElement::OnRender() {
  if (!this->GetRenderManager()) {
    return;
  }

  if (this->isDirty) {
    MeshBuilder builder(this->GetComputedValues().opacity());

    this->build(builder, this->GetBox().GetSize(Rml::BoxArea::Border), Rml::ElementUtilities::GetDensityIndependentPixelRatio(this));
    this->geometry = this->GetRenderManager()->MakeGeometry(builder.takeMesh());
    this->isDirty = false;
  }

  this->geometry.Render(this->GetAbsoluteOffset(Rml::BoxArea::Border));
}
