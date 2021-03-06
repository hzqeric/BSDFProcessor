// =================================================================== //
// Copyright (C) 2016-2019 Kimura Ryo                                  //
//                                                                     //
// This Source Code Form is subject to the terms of the Mozilla Public //
// License, v. 2.0. If a copy of the MPL was not distributed with this //
// file, You can obtain one at http://mozilla.org/MPL/2.0/.            //
// =================================================================== //

#include "MaterialData.h"

#include <QThread>

#include <libbsdf/Brdf/Analyzer.h>
#include <libbsdf/Brdf/Processor.h>

#include "ReflectanceCalculator.h"

MaterialData::MaterialData() : specularReflectances_(nullptr),
                               specularTransmittances_(nullptr),
                               fileType_(lb::UNKNOWN_FILE),
                               numInTheta_(1),
                               numInPhi_(1),
                               numWavelengths_(1),
                               reflectancesComputed_(false)
{
}

MaterialData::~MaterialData()
{
    clearData();
}

void MaterialData::setBrdf(std::shared_ptr<lb::Brdf> brdf)
{
    brdf_ = brdf;
    updateBrdf();
}

void MaterialData::setBtdf(std::shared_ptr<lb::Btdf> btdf)
{
    btdf_ = btdf;
    updateBtdf();
}

void MaterialData::setSpecularReflectances(lb::SampleSet2D* reflectances)
{
    specularReflectances_ = reflectances;

    if (specularReflectances_) {
        specularReflectances_->updateAngleAttributes();

        numInTheta_     = specularReflectances_->getNumTheta();
        numInPhi_       = specularReflectances_->getNumPhi();
        numWavelengths_ = specularReflectances_->getNumWavelengths();

        maxPerWavelength_.resize(0);
        diffuseThresholds_.resize(0);

        reflectances_.reset(specularReflectances_);
    }
}

void MaterialData::setSpecularTransmittances(lb::SampleSet2D* reflectances)
{
    specularTransmittances_ = reflectances;

    if (specularTransmittances_) {
        specularTransmittances_->updateAngleAttributes();

        numInTheta_     = specularTransmittances_->getNumTheta();
        numInPhi_       = specularTransmittances_->getNumPhi();
        numWavelengths_ = specularTransmittances_->getNumWavelengths();

        maxPerWavelength_.resize(0);
        diffuseThresholds_.resize(0);

        reflectances_.reset(specularTransmittances_);
    }
}

void MaterialData::clearData()
{
    emit stopReflectanceCalculator();

    brdf_.reset();
    btdf_.reset();

    clearComputedData();
}

void MaterialData::updateBrdf()
{
    if (!brdf_) return;

    updateSampleSet(brdf_->getSampleSet());
}

void MaterialData::updateBtdf()
{
    if (!btdf_) return;

    updateSampleSet(btdf_->getSampleSet());
}

float MaterialData::getIncomingPolarAngle(int index) const
{
    if (brdf_ || btdf_) {
        const lb::SampleSet* ss = getSampleSet();

        if (isInDirDependentCoordinateSystem()) {
            return ss->getAngle0(index);
        }
        else {
            if (numInTheta_ == 1) {
                return 0.0f;
            }
            else {
                return index * lb::SphericalCoordinateSystem::MAX_ANGLE0 / (numInTheta_ - 1);
            }
        }
    }
    else if (specularReflectances_) {
        return specularReflectances_->getTheta(index);
    }
    else if (specularTransmittances_) {
        return specularTransmittances_->getTheta(index);
    }
    else {
        return 0.0f;
    }
}

bool MaterialData::isEmpty() const
{
    return !(brdf_ || btdf_ || specularReflectances_ || specularTransmittances_);
}

float MaterialData::getIncomingAzimuthalAngle(int index) const
{
    if (brdf_ || btdf_) {
        const lb::SampleSet* ss = getSampleSet();

        if (isInDirDependentCoordinateSystem()) {
            return ss->getAngle1(index);
        }
        else {
            if (numInPhi_ == 1) {
                return 0.0f;
            }
            else {
                return index * lb::SphericalCoordinateSystem::MAX_ANGLE1 / (numInPhi_ - 1);
            }
        }
    }
    else if (specularReflectances_) {
        return specularReflectances_->getPhi(index);
    }
    else if (specularTransmittances_) {
        return specularTransmittances_->getPhi(index);
    }
    else {
        return 0.0f;
    }
}

float MaterialData::getWavelength(int index) const
{
    if (brdf_ || btdf_) {
        return getSampleSet()->getWavelength(index);
    }
    else if (specularReflectances_) {
        return specularReflectances_->getWavelength(index);
    }
    else if (specularTransmittances_) {
        return specularTransmittances_->getWavelength(index);
    }
    else {
        return 0.0f;
    }
}

lb::ColorModel MaterialData::getColorModel() const
{
    if (brdf_ || btdf_) {
        return getSampleSet()->getColorModel();
    }
    else if (specularReflectances_) {
        return specularReflectances_->getColorModel();
    }
    else if (specularTransmittances_) {
        return specularTransmittances_->getColorModel();
    }
    else {
        return lb::SPECTRAL_MODEL;
    }
}

lb::Vec3 MaterialData::getInDir(int inThetaIndex, int inPhiIndex)
{
    const lb::SampleSet* ss = getSampleSet();
    if (!ss) return lb::Vec3::Zero();

    float inTheta = getIncomingPolarAngle(inThetaIndex);
    float inPhi = getIncomingAzimuthalAngle(inPhiIndex);
    lb::Vec3 inDir = lb::SphericalCoordinateSystem::toXyz(inTheta, inPhi);

    assert(inDir.z() >= 0.0);
    return inDir;
}

lb::Brdf* MaterialData::getBrdfData() const
{
    if (brdf_) {
        return brdf_.get();
    }
    else if (btdf_) {
        return btdf_->getBrdf().get();
    }
    else {
        return nullptr;
    }
}

lb::SampleSet* MaterialData::getSampleSet() const
{
    lb::Brdf* brdf = getBrdfData();
    if (brdf) {
        return brdf->getSampleSet();
    }
    else {
        return nullptr;
    }
}

bool MaterialData::isInDirDependentCoordinateSystem() const
{
    const lb::Brdf* brdf = getBrdfData();
    if (!brdf) return false;

    return lb::isInDirDependentCoordinateSystem(*brdf);
}

lb::DataType MaterialData::getDataType() const
{
    if (brdf_) {
        return lb::BRDF_DATA;
    }
    else if (btdf_) {
        return lb::BTDF_DATA;
    }
    else {
        return lb::UNKNOWN_DATA;
    }
}

void MaterialData::editBrdf(lb::Spectrum::Scalar    glossyIntensity,
                            lb::Spectrum::Scalar    glossyShininess,
                            lb::Spectrum::Scalar    diffuseIntensity)
{
    lb::Brdf* brdf = getBrdfData();
    if (!brdf) return;

    if (!origBrdf_) {
        origBrdf_.reset(brdf->clone());
        diffuseThresholds_ = lb::findDiffuseThresholds(*origBrdf_, lb::toRadian(60.0f));
    }

    lb::editComponents(*origBrdf_, brdf, diffuseThresholds_,
                       glossyIntensity, glossyShininess, diffuseIntensity);

    computeReflectances();

    maxPerWavelength_ = findMaxPerWavelength(*brdf->getSampleSet());
}

void MaterialData::handleReflectances()
{
    reflectancesComputed_ = true;
    emit computed();
}

lb::Spectrum MaterialData::findMaxPerWavelength(const lb::SampleSet& samples)
{
    lb::Spectrum maxSp = lb::Spectrum::Zero(samples.getNumWavelengths());

    for (int i0 = 0; i0 < samples.getNumAngles0(); ++i0) {
    for (int i1 = 0; i1 < samples.getNumAngles1(); ++i1) {
    for (int i2 = 0; i2 < samples.getNumAngles2(); ++i2) {
    for (int i3 = 0; i3 < samples.getNumAngles3(); ++i3) {
        const lb::Spectrum& sp = samples.getSpectrum(i0, i1, i2, i3);
        maxSp = maxSp.cwiseMax(sp);
    }}}}

    return maxSp;
}

void MaterialData::clearComputedData()
{
    origBrdf_.reset();

    specularReflectances_ = nullptr;
    specularTransmittances_ = nullptr;

    reflectances_.reset();

    maxPerWavelength_.resize(0);
    diffuseThresholds_.resize(0);
}

void MaterialData::updateSampleSet(lb::SampleSet* ss)
{
    clearComputedData();

    ss->updateAngleAttributes();

    if (isInDirDependentCoordinateSystem()) {
        numInTheta_ = ss->getNumAngles0();
    }
    else {
        numInTheta_ = NUM_INCOMING_POLAR_ANGLES;
    }

    numInPhi_ = ss->getNumAngles1();
    numWavelengths_ = ss->getNumWavelengths();

    maxPerWavelength_ = findMaxPerWavelength(*ss);
    computeReflectances();
}

void MaterialData::computeReflectances()
{
    lbTrace << "[MaterialData::computeReflectances]";

    const lb::SampleSet* ss = getSampleSet();
    if (!ss) return;

    emit stopReflectanceCalculator();

    if (!reflectances_) {
        reflectances_.reset(new lb::SampleSet2D(numInTheta_, numInPhi_,
                                                ss->getColorModel(),
                                                ss->getNumWavelengths()));

        reflectances_->getWavelengths() = ss->getWavelengths();

        for (int inThIndex = 0; inThIndex < numInTheta_; ++inThIndex) {
            float inTheta = getIncomingPolarAngle(inThIndex);
            reflectances_->setTheta(inThIndex, inTheta);
        }

        for (int inPhIndex = 0; inPhIndex < numInPhi_; ++inPhIndex) {
            float inPhi = getIncomingAzimuthalAngle(inPhIndex);
            reflectances_->setPhi(inPhIndex, inPhi);
        }

        lb::fillSpectra(reflectances_->getSpectra(), 0.0);
    }

    reflectancesComputed_ = false;

    // Compute reflectance and transmittance.
    ReflectanceCalculator* calc;
    if (brdf_) {
        calc = new ReflectanceCalculator(reflectances_, brdf_);
    }
    else if (btdf_) {
        calc = new ReflectanceCalculator(reflectances_, btdf_);
    }
    else {
        return;
    }

    QThread* workerThread = new QThread;
    calc->moveToThread(workerThread);

    connect(workerThread,   SIGNAL(started()),                      calc,           SLOT(computeReflectances()));
    connect(this,           SIGNAL(stopReflectanceCalculator()),    calc,           SLOT(stop()));
    connect(calc,           SIGNAL(finished()),                     this,           SLOT(handleReflectances()));
    connect(calc,           SIGNAL(finished()),                     workerThread,   SLOT(quit()));
    connect(calc,           SIGNAL(stopped()),                      workerThread,   SLOT(quit()));
    connect(workerThread,   SIGNAL(finished()),                     calc,           SLOT(deleteLater()));
    connect(workerThread,   SIGNAL(finished()),                     workerThread,   SLOT(deleteLater()));

    workerThread->start(QThread::LowestPriority);
}
