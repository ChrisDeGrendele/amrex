#include <AMReX_MLEBTensorOp.H>
#include <AMReX_EBMultiFabUtil.H>
#include <AMReX_MultiFabUtil.H>
#include <AMReX_MLTensor_K.H>
#include <AMReX_MLEBTensor_K.H>

namespace amrex {

namespace {
    constexpr int kappa_num_mglevs = 1;
}

MLEBTensorOp::MLEBTensorOp (const Vector<Geometry>& a_geom,
                            const Vector<BoxArray>& a_grids,
                            const Vector<DistributionMapping>& a_dmap,
                            const LPInfo& a_info,
                            const Vector<EBFArrayBoxFactory const*>& a_factory)
{
    define(a_geom, a_grids, a_dmap, a_info, a_factory);
}

MLEBTensorOp::~MLEBTensorOp ()
{}

void
MLEBTensorOp::define (const Vector<Geometry>& a_geom,
                      const Vector<BoxArray>& a_grids,
                      const Vector<DistributionMapping>& a_dmap,
                      const LPInfo& a_info,
                      const Vector<EBFArrayBoxFactory const*>& a_factory)
{
    BL_PROFILE("MLEBTensorOp::define()");

    MLEBABecLap::define(a_geom, a_grids, a_dmap, a_info, a_factory);

    MLEBABecLap::setScalars(1.0,1.0);

    m_kappa.clear();
    m_kappa.resize(NAMRLevels());
    m_eb_kappa.resize(NAMRLevels());
    m_tauflux.resize(NAMRLevels());
    for (int amrlev = 0; amrlev < NAMRLevels(); ++amrlev) {
        m_kappa[amrlev].resize(std::min(kappa_num_mglevs,NMGLevels(amrlev)));
        m_eb_kappa[amrlev].resize(m_kappa[amrlev].size());
        m_tauflux[amrlev].resize(m_kappa[amrlev].size());
        for (int mglev = 0; mglev < m_kappa[amrlev].size(); ++mglev) {
            for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
                m_kappa[amrlev][mglev][idim].define
                    (amrex::convert(m_grids[amrlev][mglev],
                                    IntVect::TheDimensionVector(idim)),
                     m_dmap[amrlev][mglev], 1, 0,
                     MFInfo(), *m_factory[amrlev][mglev]);
                m_tauflux[amrlev][mglev][idim].define
                    (amrex::convert(m_grids[amrlev][mglev],
                                    IntVect::TheDimensionVector(idim)),
                     m_dmap[amrlev][mglev],
                     AMREX_SPACEDIM, IntVect(1)-IntVect::TheDimensionVector(idim),
                     MFInfo(), *m_factory[amrlev][mglev]);
            }
            m_eb_kappa[amrlev][mglev].define(m_grids[amrlev][mglev],
                                             m_dmap[amrlev][mglev],
                                             1, 0, MFInfo(),
                                             *m_factory[amrlev][mglev]);
        }
    }
}

void
MLEBTensorOp::setShearViscosity (int amrlev, const Array<MultiFab const*,AMREX_SPACEDIM>& eta)
{
    MLEBABecLap::setBCoeffs(amrlev, eta);
}

void
MLEBTensorOp::setBulkViscosity (int amrlev, const Array<MultiFab const*,AMREX_SPACEDIM>& kappa)
{
    for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
        MultiFab::Copy(m_kappa[amrlev][0][idim], *kappa[idim], 0, 0, 1, 0);
    }
    m_has_kappa = true;
}

void
MLEBTensorOp::setEBShearViscosity (int amrlev, MultiFab const& eta)
{
    MLEBABecLap::setEBHomogDirichlet(amrlev, eta);
}

void
MLEBTensorOp::setEBBulkViscosity (int amrlev, MultiFab const& kappa)
{
    MultiFab::Copy(m_eb_kappa[amrlev][0], kappa, 0, 0, 1, 0);
    m_has_eb_kappa = true;
}

void
MLEBTensorOp::prepareForSolve ()
{
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(m_has_kappa == m_has_eb_kappa,
        "MLEBTensorOp: must call both setBulkViscosity and setEBBulkViscosity or none.");

    if (m_has_kappa) {
        amrex::Abort("MLEBTensorOp::prepareForSolve: TODO m_has_kappa");
    } else {
        for (int amrlev = 0; amrlev < NAMRLevels(); ++amrlev) {
            for (int mglev = 0; mglev < m_kappa[amrlev].size(); ++mglev) {
                for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
                    m_kappa[amrlev][mglev][idim].setVal(0.0);
                }
            }
        }
    }

    if (m_has_eb_kappa) {
        amrex::Abort("MLEBTensorOp::prepareForSolve: TODO m_has_eb_kappa");
    } else {
        for (int amrlev = 0; amrlev < NAMRLevels(); ++amrlev) {
            for (int mglev = 0; mglev < m_eb_kappa[amrlev].size(); ++mglev) {
                m_eb_kappa[amrlev][mglev].setVal(0.0);
            }
        }
    }

    for (int amrlev = 0; amrlev < NAMRLevels(); ++amrlev) {
        for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
            int icomp = idim;
            MultiFab::Xpay(m_b_coeffs[amrlev][0][idim], 4./3.,
                           m_kappa[amrlev][0][idim], 0, icomp, 1, 0);
        }
    }

    MLEBABecLap::prepareForSolve();
}

void
MLEBTensorOp::apply (int amrlev, int mglev, MultiFab& out, MultiFab& in, BCMode bc_mode,
                     StateMode s_mode, const MLMGBndry* bndry) const
{
    BL_PROFILE("MLEBTensorOp::apply()");
    MLEBABecLap::apply(amrlev, mglev, out, in, bc_mode, s_mode, bndry);

    if (mglev >= m_kappa[amrlev].size()) return;

    applyBCTensor(amrlev, mglev, in, bc_mode, bndry);

    // todo: gpu
    Gpu::LaunchSafeGuard lg(false);

    auto factory = dynamic_cast<EBFArrayBoxFactory const*>(m_factory[amrlev][mglev].get());
    const FabArray<EBCellFlagFab>* flags = (factory) ? &(factory->getMultiEBCellFlagFab()) : nullptr;
    const MultiFab* vfrac = (factory) ? &(factory->getVolFrac()) : nullptr;
    auto area = (factory) ? factory->getAreaFrac()
        : Array<const MultiCutFab*,AMREX_SPACEDIM>{AMREX_D_DECL(nullptr,nullptr,nullptr)};
    auto fcent = (factory) ? factory->getFaceCent()
        : Array<const MultiCutFab*,AMREX_SPACEDIM>{AMREX_D_DECL(nullptr,nullptr,nullptr)};
    const MultiCutFab* barea = (factory) ? &(factory->getBndryArea()) : nullptr;
    const MultiCutFab* bcent = (factory) ? &(factory->getBndryCent()) : nullptr;

    const int is_eb_dirichlet = true;

    const Geometry& geom = m_geom[amrlev][mglev];
    const auto dxinv = geom.InvCellSizeArray();

    Array<MultiFab,AMREX_SPACEDIM> const& etamf = m_b_coeffs[amrlev][mglev];
    Array<MultiFab,AMREX_SPACEDIM> const& kapmf = m_kappa[amrlev][mglev];
    Array<MultiFab,AMREX_SPACEDIM>& fluxmf = m_tauflux[amrlev][mglev];

    if (Gpu::inLaunchRegion())
    {
        for (MFIter mfi(out); mfi.isValid(); ++mfi)
        {
            const Box& bx = mfi.tilebox();
            AMREX_D_TERM(Box const xbx = amrex::surroundingNodes(bx,0);,
                         Box const ybx = amrex::surroundingNodes(bx,1);,
                         Box const zbx = amrex::surroundingNodes(bx,2););

            auto fabtyp = (flags) ? (*flags)[mfi].getType(bx) : FabType::regular;

            if (fabtyp == FabType::covered) {
                AMREX_D_TERM(FArrayBox* fxfab = fluxmf[0].fabPtr(mfi);,
                             FArrayBox* fyfab = fluxmf[1].fabPtr(mfi);,
                             FArrayBox* fzfab = fluxmf[2].fabPtr(mfi););
                AMREX_LAUNCH_HOST_DEVICE_LAMBDA
                ( xbx, txbx,
                  {
                      fxfab->setVal(0.0, txbx, 0, AMREX_SPACEDIM);
                  }
                , ybx, tybx,
                  {
                      fyfab->setVal(0.0, tybx, 0, AMREX_SPACEDIM);
                  }
#if (AMREX_SPACEDIM == 3)
                , zbx, tzbx,
                  {
                      fzfab->setVal(0.0, tzbx, 0, AMREX_SPACEDIM);
                  }
#endif
                );
            } else {
                AMREX_D_TERM(Array4<Real> const fxfab = fluxmf[0].array(mfi);,
                             Array4<Real> const fyfab = fluxmf[1].array(mfi);,
                             Array4<Real> const fzfab = fluxmf[2].array(mfi););
                Array4<Real const> const vfab = in.array(mfi);
                AMREX_D_TERM(Array4<Real const> const etaxfab = etamf[0].array(mfi);,
                             Array4<Real const> const etayfab = etamf[1].array(mfi);,
                             Array4<Real const> const etazfab = etamf[2].array(mfi););
                AMREX_D_TERM(Array4<Real const> const kapxfab = kapmf[0].array(mfi);,
                             Array4<Real const> const kapyfab = kapmf[1].array(mfi);,
                             Array4<Real const> const kapzfab = kapmf[2].array(mfi););

                if (fabtyp == FabType::regular)
                {
                    AMREX_LAUNCH_HOST_DEVICE_LAMBDA
                    ( xbx, txbx,
                      {
                          mltensor_cross_terms_fx(txbx,fxfab,vfab,etaxfab,kapxfab,dxinv);
                      }
                    , ybx, tybx,
                      {
                          mltensor_cross_terms_fy(tybx,fyfab,vfab,etayfab,kapyfab,dxinv);
                      }
#if (AMREX_SPACEDIM == 3)
                    , zbx, tzbx,
                      {
                          mltensor_cross_terms_fz(tzbx,fzfab,vfab,etazfab,kapzfab,dxinv);
                      }
#endif
                    );
                }
                else
                {
                    AMREX_D_TERM(Array4<Real const> const& apx = area[0]->array(mfi);,
                                 Array4<Real const> const& apy = area[1]->array(mfi);,
                                 Array4<Real const> const& apz = area[2]->array(mfi););
                    Array4<EBCellFlag const> const& flag = flags->array(mfi);

                    AMREX_LAUNCH_HOST_DEVICE_LAMBDA
                    ( xbx, txbx,
                      {
                          mlebtensor_cross_terms_fx(txbx,fxfab,vfab,etaxfab,kapxfab,apx,flag,dxinv);
                      }
                    , ybx, tybx,
                      {
                          mlebtensor_cross_terms_fy(tybx,fyfab,vfab,etayfab,kapyfab,apy,flag,dxinv);
                      }
#if (AMREX_SPACEDIM == 3)
                    , zbx, tzbx,
                      {
                          mlebtensor_cross_terms_fz(tzbx,fzfab,vfab,etazfab,kapzfab,apz,flag,dxinv);
                      }
#endif
                    );
                }
            }
        }
    }
    else
    {
#ifdef _OPENMP
#pragma omp parallel
#endif
    {
        FArrayBox fluxfab_tmp[AMREX_SPACEDIM];
        for (MFIter mfi(out,MFItInfo().EnableTiling().SetDynamic(true)); mfi.isValid(); ++mfi)
        {
            const Box& bx = mfi.tilebox();
            AMREX_D_TERM(Box const xbx = mfi.nodaltilebox(0);,
                         Box const ybx = mfi.nodaltilebox(1);,
                         Box const zbx = mfi.nodaltilebox(2););
            AMREX_D_TERM(FArrayBox* fxfab = fluxmf[0].fabPtr(mfi);,
                         FArrayBox* fyfab = fluxmf[1].fabPtr(mfi);,
                         FArrayBox* fzfab = fluxmf[2].fabPtr(mfi););

            auto fabtyp = (flags) ? (*flags)[mfi].getType(bx) : FabType::regular;

            if (fabtyp == FabType::covered) {
                AMREX_D_TERM(fxfab->setVal(0.0, xbx, 0, AMREX_SPACEDIM);,
                             fyfab->setVal(0.0, ybx, 0, AMREX_SPACEDIM);,
                             fzfab->setVal(0.0, zbx, 0, AMREX_SPACEDIM););
            } else {
                Array4<Real const> const vfab = in.array(mfi);
                AMREX_D_TERM(Array4<Real const> const etaxfab = etamf[0].array(mfi);,
                             Array4<Real const> const etayfab = etamf[1].array(mfi);,
                             Array4<Real const> const etazfab = etamf[2].array(mfi););
                AMREX_D_TERM(Array4<Real const> const kapxfab = kapmf[0].array(mfi);,
                             Array4<Real const> const kapyfab = kapmf[1].array(mfi);,
                             Array4<Real const> const kapzfab = kapmf[2].array(mfi););
                AMREX_D_TERM(fluxfab_tmp[0].resize(xbx,AMREX_SPACEDIM);,
                             fluxfab_tmp[1].resize(ybx,AMREX_SPACEDIM);,
                             fluxfab_tmp[2].resize(zbx,AMREX_SPACEDIM););

                if (fabtyp == FabType::regular)
                {
                    mltensor_cross_terms_fx(xbx,fluxfab_tmp[0].array(),vfab,etaxfab,kapxfab,dxinv);
                    mltensor_cross_terms_fy(ybx,fluxfab_tmp[1].array(),vfab,etayfab,kapyfab,dxinv);
#if (AMREX_SPACEDIM == 3)
                    mltensor_cross_terms_fz(zbx,fluxfab_tmp[2].array(),vfab,etazfab,kapzfab,dxinv);
#endif
                }
                else
                {
                    AMREX_D_TERM(Array4<Real const> const& apx = area[0]->array(mfi);,
                                 Array4<Real const> const& apy = area[1]->array(mfi);,
                                 Array4<Real const> const& apz = area[2]->array(mfi););
                    Array4<EBCellFlag const> const& flag = flags->array(mfi);

                    mlebtensor_cross_terms_fx(xbx,fluxfab_tmp[0].array(),vfab,etaxfab,kapxfab,
                                              apx,flag,dxinv);
                    mlebtensor_cross_terms_fy(ybx,fluxfab_tmp[1].array(),vfab,etayfab,kapyfab,
                                              apy,flag,dxinv);
#if (AMREX_SPACEDIM == 3)
                    mlebtensor_cross_terms_fz(zbx,fluxfab_tmp[2].array(),vfab,etazfab,kapzfab,
                                              apz,flag,dxinv);
#endif
                }

                AMREX_D_TERM(fxfab->copy(fluxfab_tmp[0], xbx, 0, xbx, 0, AMREX_SPACEDIM);,
                             fyfab->copy(fluxfab_tmp[1], ybx, 0, ybx, 0, AMREX_SPACEDIM);,
                             fzfab->copy(fluxfab_tmp[2], zbx, 0, zbx, 0, AMREX_SPACEDIM););
            }
        }
    }
    }

    for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
        fluxmf[idim].FillBoundary(0, AMREX_SPACEDIM, geom.periodicity());
    }

    MFItInfo mfi_info;
    if (Gpu::notInLaunchRegion()) mfi_info.EnableTiling().SetDynamic(true);
#ifdef _OPENMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
    for (MFIter mfi(out, mfi_info); mfi.isValid(); ++mfi)
    {
        const Box& bx = mfi.tilebox();

        auto fabtyp = (flags) ? (*flags)[mfi].getType(bx) : FabType::regular;
        if (fabtyp == FabType::covered) continue;

        Array4<Real> const axfab = out.array(mfi);
        AMREX_D_TERM(Array4<Real const> const fxfab = fluxmf[0].array(mfi);,
                     Array4<Real const> const fyfab = fluxmf[1].array(mfi);,
                     Array4<Real const> const fzfab = fluxmf[2].array(mfi););

        if (fabtyp == FabType::regular)
        {
            AMREX_LAUNCH_HOST_DEVICE_LAMBDA ( bx, tbx,
            {
                mltensor_cross_terms(tbx, axfab, AMREX_D_DECL(fxfab,fyfab,fzfab), dxinv);
            });
        }
        else
        {
            AMREX_D_TERM(Array4<Real const> const& apx = area[0]->array(mfi);,
                         Array4<Real const> const& apy = area[1]->array(mfi);,
                         Array4<Real const> const& apz = area[2]->array(mfi););
            Array4<EBCellFlag const> const& flag = flags->array(mfi);
            Array4<Real const> const& vol = vfrac->array(mfi);
        }
    }
}

void
MLEBTensorOp::applyBCTensor (int amrlev, int mglev, MultiFab& vel,
                             BCMode bc_mode, const MLMGBndry* bndry) const
{
    // Corners have been filled in MLEBABecLap::applyBC.
    return;
}

}