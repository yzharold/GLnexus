#include <iostream>
#include "genotyper.h"
#include "types.h"
#include "catch.hpp"
using namespace std;
using namespace GLnexus;

TEST_CASE("One_call_ordering") {

    SECTION("Numerical calls") {
        one_call call1=one_call(1, NoCallReason::N_A);
        one_call call2=one_call(2, NoCallReason::N_A);
        REQUIRE(call2 > call1);
    }

    SECTION("No call"){
        one_call call1 = one_call();
        one_call call2 = one_call(0, NoCallReason::N_A);
        REQUIRE(call2 < call1);
    }

}

Status test_vcf1(const char* vcf, shared_ptr<bcf_hdr_t>& hdr, shared_ptr<bcf1_t>& record) {
    const char *tmpfn = "/tmp/GLnexus_genotyper_test.vcf";
    ofstream ofs;
    ofs.open(tmpfn, ofstream::out);
    if (!ofs.good()) return Status::IOError("test_vcf1 ofstream");
    ofs << vcf;
    ofs.close();

    shared_ptr<vcfFile> f(vcf_open(tmpfn, "r"), [](vcfFile* h) { vcf_close(h); });
    hdr = shared_ptr<bcf_hdr_t>(bcf_hdr_read(f.get()), &bcf_hdr_destroy);
    record = shared_ptr<bcf1_t>(bcf_init1(), &bcf_destroy);
    if (bcf_read1(f.get(), hdr.get(), record.get())) return Status::IOError("test_vcf1 bcf_read1");
    if (bcf_unpack(record.get(), BCF_UN_ALL)) return Status::IOError("test_vcf1 bcf_unpack");
    return Status::OK();
}

TEST_CASE("revise_genotypes") {
    const char* genotyper_cfg_yml = 1 + R"(
revise_genotypes: true
min_assumed_allele_frequency: 0.0001
liftover_fields:
- orig_names: [GQ]
  name: GQ
  description: '##FORMAT=<ID=GQ,Number=1,Type=Integer,Description="Genotype Quality">'
  type: int
  number: basic
  combi_method: min
  count: 1
  ignore_non_variants: true
)";
    YAML::Node yaml = YAML::Load(genotyper_cfg_yml);
    genotyper_config genotyper_cfg;
    Status s = GLnexus::genotyper_config::of_yaml(yaml, genotyper_cfg);
    REQUIRE(s.ok());

    const char* us_yml = 1 + R"(
range: {ref: "21", beg: 1000, end: 1000}
alleles: [T, A, G]
allele_frequencies: [.nan, 0.01, 0.00001]
lost_allele_frequency: 0.001
unification:
- range: {beg: 1000, end: 1000}
  dna: A
  to: 1
- range: {beg: 1000, end: 1000}
  dna: G
  to: 2
)";
    yaml = YAML::Load(us_yml);
    unified_site us(range(-1,-1,-1));
    s = unified_site::of_yaml(yaml, {make_pair(string("21"),10000)}, us);
    REQUIRE(s.ok());

    const char* header_txt = 1 + R"eof(
##fileformat=VCFv4.1
##FILTER=<ID=PASS,Description="All filters passed">
##ALT=<ID=NON_REF,Description="Represents any possible alternative allele at this location">
##FILTER=<ID=LowQual,Description="Low quality">
##FORMAT=<ID=AD,Number=.,Type=Integer,Description="Allelic depths for the ref and alt alleles in the order listed">
##FORMAT=<ID=DP,Number=1,Type=Integer,Description="Approximate read depth (reads with MQ=255 or with bad mates are filtered)">
##FORMAT=<ID=GQ,Number=1,Type=Integer,Description="Genotype Quality">
##FORMAT=<ID=GT,Number=1,Type=String,Description="Genotype">
##FORMAT=<ID=MIN_DP,Number=1,Type=Integer,Description="Minimum DP observed within the GVCF block">
##FORMAT=<ID=PL,Number=G,Type=Integer,Description="Normalized, Phred-scaled likelihoods for genotypes as defined in the VCF specification">
##FORMAT=<ID=SB,Number=4,Type=Integer,Description="Per-sample component statistics which comprise the Fisher's Exact Test to detect strand">
##INFO=<ID=END,Number=1,Type=Integer,Description="Stop position of the interval">
##contig=<ID=21,length=48129895>
#CHROM	POS	ID	REF	ALT	QUAL	FILTER	INFO	FORMAT	A
)eof";

    map<int,int> sample_mapping;
    sample_mapping[0] = 0;

    shared_ptr<bcf_hdr_t> hdr;
    shared_ptr<bcf1_t> rec;

    SECTION("revision") {
        const char* vcf_txt = "21	1000	.	T	A,<NON_REF>	.	.	.	GT:AD:DP:GQ:PL:SB	0/1:10,2,0:12:16:16,0,240,46,246,292:1,9,1,1";
        s = test_vcf1((string(header_txt)+vcf_txt).c_str(), hdr, rec);
        REQUIRE(s.ok());

        s = GLnexus::revise_genotypes(genotyper_cfg, us, sample_mapping, hdr.get(), rec.get());
        REQUIRE(s.ok());

        htsvecbox<int> gt;
        REQUIRE(bcf_get_genotypes(hdr.get(), rec.get(), &gt.v, &gt.capacity) == 2);
        REQUIRE(!bcf_gt_is_missing(gt[0]));
        REQUIRE(bcf_gt_allele(gt[0]) == 0);
        REQUIRE(!bcf_gt_is_missing(gt[1]));
        REQUIRE(bcf_gt_allele(gt[1]) == 0);

        htsvecbox<int32_t> gq;
        REQUIRE(bcf_get_format_int32(hdr.get(), rec.get(), "GQ", &gq.v, &gq.capacity) == 1);
        REQUIRE(gq[0] == 4);
    }

    SECTION("no revision") {
        const char* vcf_txt = "21	1000	.	T	A,<NON_REF>	.	.	.	GT:AD:DP:GQ:PL	0/1:10,10,0:20:60:60,0,240,80,246,292";
        s = test_vcf1((string(header_txt)+vcf_txt).c_str(), hdr, rec);
        REQUIRE(s.ok());

        s = GLnexus::revise_genotypes(genotyper_cfg, us, sample_mapping, hdr.get(), rec.get());
        REQUIRE(s.ok());

        htsvecbox<int> gt;
        REQUIRE(bcf_get_genotypes(hdr.get(), rec.get(), &gt.v, &gt.capacity) == 2);
        REQUIRE(!bcf_gt_is_missing(gt[0]));
        REQUIRE(bcf_gt_allele(gt[0]) == 0);
        REQUIRE(!bcf_gt_is_missing(gt[1]));
        REQUIRE(bcf_gt_allele(gt[1]) == 1);

        htsvecbox<int32_t> gq;
        REQUIRE(bcf_get_format_int32(hdr.get(), rec.get(), "GQ", &gq.v, &gq.capacity) == 1);
        REQUIRE(gq[0] == 40);
    }

    SECTION("min_assumed_allele_frequency revision") {
        const char* vcf_txt = "21	1000	.	T	G,<NON_REF>	.	.	.	GT:AD:DP:GQ:PL:SB	0/1:10,2,0:12:16:16,0,240,46,246,292:1,9,1,1";
        s = test_vcf1((string(header_txt)+vcf_txt).c_str(), hdr, rec);
        REQUIRE(s.ok());

        s = GLnexus::revise_genotypes(genotyper_cfg, us, sample_mapping, hdr.get(), rec.get());
        REQUIRE(s.ok());

        htsvecbox<int> gt;
        REQUIRE(bcf_get_genotypes(hdr.get(), rec.get(), &gt.v, &gt.capacity) == 2);
        REQUIRE(!bcf_gt_is_missing(gt[0]));
        REQUIRE(bcf_gt_allele(gt[0]) == 0);
        REQUIRE(!bcf_gt_is_missing(gt[1]));
        REQUIRE(bcf_gt_allele(gt[1]) == 0);

        // likelihood is adjusted by -40 even though unified allele frequency corresponds to -50
        htsvecbox<int32_t> gq;
        REQUIRE(bcf_get_format_int32(hdr.get(), rec.get(), "GQ", &gq.v, &gq.capacity) == 1);
        REQUIRE(gq[0] == 24);
    }

    SECTION("min_assumed_allele_frequency no revision") {
        const char* vcf_txt = "21	1000	.	T	G,<NON_REF>	.	.	.	GT:AD:DP:GQ:PL	0/1:10,6,0:16:45:45,0,240,86,246,292";
        s = test_vcf1((string(header_txt)+vcf_txt).c_str(), hdr, rec);
        REQUIRE(s.ok());

        s = GLnexus::revise_genotypes(genotyper_cfg, us, sample_mapping, hdr.get(), rec.get());
        REQUIRE(s.ok());

        htsvecbox<int> gt;
        REQUIRE(bcf_get_genotypes(hdr.get(), rec.get(), &gt.v, &gt.capacity) == 2);
        REQUIRE(!bcf_gt_is_missing(gt[0]));
        REQUIRE(bcf_gt_allele(gt[0]) == 0);
        REQUIRE(!bcf_gt_is_missing(gt[1]));
        REQUIRE(bcf_gt_allele(gt[1]) == 1);

        // likelihood is adjusted by -40 even though unified allele frequency corresponds to -50
        htsvecbox<int32_t> gq;
        REQUIRE(bcf_get_format_int32(hdr.get(), rec.get(), "GQ", &gq.v, &gq.capacity) == 1);
        REQUIRE(gq[0] == 5);
    }

    SECTION("lost allele") {
        const char* vcf_txt = "21	1000	.	T	C,<NON_REF>	.	.	.	GT:AD:DP:GQ:PL	0/1:10,5,0:15:45:45,0,240,86,246,292";
        s = test_vcf1((string(header_txt)+vcf_txt).c_str(), hdr, rec);
        REQUIRE(s.ok());

        s = GLnexus::revise_genotypes(genotyper_cfg, us, sample_mapping, hdr.get(), rec.get());
        REQUIRE(s.ok());

        htsvecbox<int> gt;
        REQUIRE(bcf_get_genotypes(hdr.get(), rec.get(), &gt.v, &gt.capacity) == 2);
        REQUIRE(!bcf_gt_is_missing(gt[0]));
        REQUIRE(bcf_gt_allele(gt[0]) == 0);
        REQUIRE(!bcf_gt_is_missing(gt[1]));
        REQUIRE(bcf_gt_allele(gt[1]) == 1);

        htsvecbox<int32_t> gq;
        REQUIRE(bcf_get_format_int32(hdr.get(), rec.get(), "GQ", &gq.v, &gq.capacity) == 1);
        REQUIRE(gq[0] == 15);
    }
}
