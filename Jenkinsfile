// Global variable initialization
buildNumber = currentBuild.id
version = null
release_version = ""
branchName = ""
commitId = null
repoName = null
changelog = ""

pipeline {
    agent none

    options {
        parallelsAlwaysFailFast()
        disableConcurrentBuilds()
    }

    // NOTE: checks every 5 minutes if a new commit occurred after last successful run
    triggers {
        pollSCM 'H/5 * * * *'
    }

    stages {
       stage('Test + Build') {
	    agent {
		dockerfile true
	    }
	    steps {
		script {
		    def vcs = checkout([
			    $class: 'GitSCM',
			    changelog: true,
			    userRemoteConfigs: scm.userRemoteConfigs,
			    branches: scm.branches,
			    extensions: scm.extensions + [
				    [
				    $class: 'SubmoduleOption',
				    disableSubmodules: false,
				    recursiveSubmodules: true,
				    parentCredentials: true
				    ]
			    ]
		    ])

		    branchName = vcs.GIT_BRANCH
		    commitId = "${vcs.GIT_COMMIT}"[0..6]
		    repoName = vcs.GIT_URL.tokenize( '/' ).last().tokenize( '.' ).first()

		    release_version = branchName.replaceAll("[^\\d\\.]", "");
		    if (release_version.length() > 0 || branchName.contains('release')) {
			version = release_version + "." + buildNumber
		    } else {
			version = "0.0.${buildNumber}"
		    }
		}

		// clean
		sh 'make distclean'
		sh 'touch src/version/version.c'

		// build tests
		sh 'make test AVS_VERSION=' + version
		// run tests
		sh './ztest'

		sh 'make dist_clean'
		sh 'make zcall AVS_VERSION=' + version
		sh '''#!/bin/bash
		    . ./scripts/wasm_devenv.sh && make dist_linux dist_wasm AVS_VERSION=''' + version + '  BUILDVERSION=' + version + '''
		'''
		sh 'rm -rf ./build/artifacts'
		sh 'mkdir -p ./build/artifacts'
		sh 'cp ./build/dist/linux/avscore.tar.bz2 ./build/artifacts/avs.linux.' + version + '.tar.bz2'
		sh 'zip -9j ./build/artifacts/zcall_linux_' + version + '.zip ./zcall'
		sh 'cp ./build/dist/wasm/wireapp-avs-' + version + '.tgz ./build/artifacts/'

		archiveArtifacts artifacts: 'build/artifacts/*', followSymlinks: false
	    }
        }
        stage('Prepare changelog') {
            steps {
                script {
                    currentBuild.changeSets.each { set ->
                        set.items.each { entry ->
                            changelog += '- ' + entry.msg + '\n'
                        }
                    }
                }
                echo("Changelog:")
                echo(changelog)
            }
        }
        stage('Tag + Create Github release') {
            agent {
                label "linuxbuild"
            }
            when {
                anyOf {
                    expression { return "${branchName}".contains('release') || "${branchName}".contains('main') }
                }
            }

            steps {
                echo "Tag as ${version}"
                withCredentials([sshUserPrivateKey(credentialsId: 'wire-avs', keyFileVariable: 'sshPrivateKeyPath')]) {
                    sh(
                        script: """
                            cd "${env.WORKSPACE}"

                            git tag ${version}

                            git \
                                -c core.sshCommand='ssh -i ${sshPrivateKeyPath}' \
                                push \
                                origin ${version}
                        """
                    )
                }
                echo 'Creating release on Github'
                // Unfortunately it is not allowed to access github API with a deploy key so we have to rely on
                // a user token to upload via python github package
                withCredentials([ string( credentialsId: 'github-repo-user', variable: 'repoUser' ),
                    string( credentialsId: 'github-repo-access', variable: 'accessToken' ) ]) {
                    // NOTE: creating an empty stub directory just to create the release
                    sh(
                        script: """
                            cd "${env.WORKSPACE}"

                            GITHUB_USER=${repoUser} \
                            GITHUB_TOKEN=${accessToken} \
                            python3 ./scripts/release-on-github.py \
                                ${repoName} \
                                \$(mktemp -d) \
                                ${version} \
                                "${changelog}"
                        """
                    )
                }
            }
        }
        stage('Upload to Github') {
            when {
                anyOf {
                    expression { return "${branchName}".contains('release') || "${branchName}".contains('main') }
                }
            }
            matrix {
                axes {
                    axis {
                        name 'AGENT'
                        values 'linuxbuild'
                    }
                }
                agent {
                    label "${AGENT}"
                }

                stages {
                    stage( 'Uploading release artifacts' ) {
                        steps {
                            withCredentials([ string( credentialsId: 'github-repo-user', variable: 'repoUser' ),
                                string( credentialsId: 'github-repo-access', variable: 'accessToken' ) ]) {
                                sh(
                                    script: """
                                        GITHUB_USER=${repoUser} \
                                        GITHUB_TOKEN=${accessToken} \
                                        python3 ./scripts/release-on-github.py \
                                            ${repoName} \
                                            ./build/artifacts \
                                            ${version} \
                                            "${changelog}"
                                    """
                                )
                            }
                        }
                    }
                }
            }
        }
        stage('Publish to npm') {
            when {
                anyOf {
                    expression { return "${branchName}".contains('release') }
                }
            }
            agent {
                label 'linuxbuild'
            }
            steps {
                // NOTE: the script upload-wasm.sh supports non-release branches, but in the past
                //       it still was only invoked on release branches
                nodejs("18.12.1") {
                    withCredentials([ string( credentialsId: 'npmtoken', variable: 'accessToken' ) ]) {
                        sh """
                            # NOTE: upload-wasm.sh assumes a certain current working directory
                            NPM_TOKEN=${accessToken} \
                            ${env.WORKSPACE}/scripts/upload-wasm.sh avs-release-${release_version}
                        """
                    }
                }
            }
        }
    }

    post {
        always {
            node('linuxbuild') {
                script {
                    sh 'docker container prune -f && docker volume prune -f && docker image prune -f'
                }
            }
        }

        success {
            node( 'm1' ) {
                withCredentials([ string( credentialsId: 'wire-jenkinsbot', variable: 'jenkinsbot_secret' ) ]) {
                    wireSend secret: "$jenkinsbot_secret", message: "✅ ${JOB_NAME} #${BUILD_ID} succeeded\n**Changelog:**\n${changelog}\n${BUILD_URL}console\nhttps://github.com/wireapp/wire-avs/commit/${commitId}"
                }
            }
        }

        failure {
            node( 'm1' ) {
                withCredentials([ string( credentialsId: 'wire-jenkinsbot', variable: 'jenkinsbot_secret' ) ]) {
                    wireSend secret: "$jenkinsbot_secret", message: "❌ ${JOB_NAME} #${BUILD_ID} failed\n${BUILD_URL}console\nhttps://github.com/wireapp/wire-avs/commit/${commitId}"
                }
            }
        }
    }
}
